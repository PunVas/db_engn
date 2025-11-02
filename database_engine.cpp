// main db engine file
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>

namespace CFG{
	constexpr size_t P_SZ=4096;
	constexpr size_t C_SZ=100;
	constexpr size_t B_ORD=64;
	constexpr size_t K_SZ=256;
	constexpr size_t V_SZ=1024;
	const std::string D_FILE="database.dat";
	const std::string I_FILE="index.dat";
	const std::string J_FILE="journal.log";
}

// core data struct
struct Rec{
	char key[CFG::K_SZ];
	char val[CFG::V_SZ];
	uint64_t pid;
	bool del;
	
	Rec():pid(0),del(false){
		memset(key,0,CFG::K_SZ);
		memset(val,0,CFG::V_SZ);
	}
	
	Rec(const std::string& k,const std::string& v,uint64_t p=0)
		:pid(p),del(false){
		strncpy(key,k.c_str(),CFG::K_SZ-1);
		strncpy(val,v.c_str(),CFG::V_SZ-1);
	}
	
	std::string getK() const{return std::string(key);}
	std::string getV() const{return std::string(val);}
};

struct Pg{
	uint64_t pid;
	char data[CFG::P_SZ];
	bool drty;
	
	Pg(uint64_t id=0):pid(id),drty(false){
		memset(data,0,CFG::P_SZ);
	}
	
	void wRec(const Rec& rec){
		memcpy(data,&rec,sizeof(Rec));
		drty=true;
	}
	
	Rec rRec() const{
	Rec rec;
		memcpy(&rec,data,sizeof(Rec));
		return rec;
	}
};

// WAL log
class JMan{
private:
	std::fstream jFile;
	
	enum Op{INS,UPD,DEL,CMT};
	
	struct JEnt{
		Op op;
		char key[CFG::K_SZ];
		char val[CFG::V_SZ];
		uint64_t pid;
		
		JEnt():op(INS),pid(0){
			memset(key,0,CFG::K_SZ);
			memset(val,0,CFG::V_SZ);
		}
	};
	
public:
	JMan(){
		jFile.open(CFG::J_FILE,
			std::ios::in|std::ios::out|std::ios::binary|std::ios::app);
		if(!jFile.is_open()){
			jFile.open(CFG::J_FILE,
				std::ios::out|std::ios::binary);
			jFile.close();
			jFile.open(CFG::J_FILE,
				std::ios::in|std::ios::out|std::ios::binary);
		}
	}
	
	~JMan(){
		if(jFile.is_open()){
			jFile.close();
		}
	}
	
	void logOp(const std::string& opType,const std::string& key,
						 const std::string& val="",uint64_t pid=0){
		JEnt ent;
		
		if(opType=="INSERT")ent.op=INS;
		else if(opType=="UPDATE")ent.op=UPD;
		else if(opType=="DELETE")ent.op=DEL;
		else if(opType=="COMMIT")ent.op=CMT;
		
		strncpy(ent.key,key.c_str(),CFG::K_SZ-1);
		strncpy(ent.val,val.c_str(),CFG::V_SZ-1);
		ent.pid=pid;
		
		jFile.seekp(0,std::ios::end);
		jFile.write(reinterpret_cast<char*>(&ent),sizeof(JEnt));
		jFile.flush(); // need this for safety
	}
	
	void commit(){
		logOp("COMMIT","");
	}
	
	void trunc(){
		jFile.close();
		std::remove(CFG::J_FILE.c_str());
		jFile.open(CFG::J_FILE,
			std::ios::in|std::ios::out|std::ios::binary|std::ios::trunc);
	}
};

// the cache
class BPool{
private:
	struct CEnt{
		std::shared_ptr<Pg> pg;
		size_t tm;
	};
	
	std::map<uint64_t,CEnt> cache;
	size_t curTm;
	
public:
	BPool():curTm(0){}
	
	std::shared_ptr<Pg> get(uint64_t pid){
		auto it=cache.find(pid);
		if(it!=cache.end()){
			it->second.tm=++curTm;
			return it->second.pg;
		}
		return nullptr;
	}
	
	void put(uint64_t pid,std::shared_ptr<Pg> pg){
		if(cache.size()>=CFG::C_SZ){
			evict();
		}
		cache[pid]={pg,++curTm};
	}
	
	void evict(){
		auto old=cache.begin();
		for(auto it=cache.begin();it!=cache.end();++it){
			if(it->second.tm<old->second.tm){
				old=it;
			}
		}
		if(old!=cache.end()){
			cache.erase(old);
		}
	}
	
	std::vector<std::shared_ptr<Pg>> getDirty(){
		std::vector<std::shared_ptr<Pg>> dirty;
		for(auto& pair:cache){
			if(pair.second.pg->drty){
				dirty.push_back(pair.second.pg);
			}
		}
		return dirty;
	}
	
	void clear(){
		cache.clear();
	}
};

// magic B-Tree part
struct BNode{
	bool leaf;
	std::vector<std::string> keys;
	std::vector<uint64_t> vals;
	std::vector<std::shared_ptr<BNode>> kids;
	std::shared_ptr<BNode> next;
	
	BNode(bool l=true):leaf(l),next(nullptr){}
	
	size_t findPos(const std::string& key) const{
		return std::lower_bound(keys.begin(),keys.end(),key)-keys.begin();
	}
};

class BTree{
private:
	std::shared_ptr<BNode> root;
	size_t ord;
	
	std::shared_ptr<BNode> split(std::shared_ptr<BNode> node){
		auto newNode=std::make_shared<BNode>(node->leaf);
		size_t mid=node->keys.size()/2;
		
		if(node->leaf){
			newNode->keys.assign(node->keys.begin()+mid,node->keys.end());
			newNode->vals.assign(node->vals.begin()+mid,node->vals.end());
			newNode->next=node->next;
			node->next=newNode;
			
			node->keys.resize(mid);
			node->vals.resize(mid);
		}else{
			newNode->keys.assign(node->keys.begin()+mid+1,node->keys.end());
			newNode->kids.assign(node->kids.begin()+mid+1,
									   node->kids.end());
			
			node->keys.resize(mid);
			node->kids.resize(mid+1);
		}
		
		return newNode;
	}
	
	std::pair<std::shared_ptr<BNode>,std::string>
	insInt(std::shared_ptr<BNode> node,const std::string& key,
				 uint64_t val){
		if(node->leaf){
			size_t pos=node->findPos(key);
			
			if(pos<node->keys.size()&&node->keys[pos]==key){
				node->vals[pos]=val; // update
				return std::make_pair(nullptr,"");
			}
			
			node->keys.insert(node->keys.begin()+pos,key);
			node->vals.insert(node->vals.begin()+pos,val);
			
			if(node->keys.size()>=ord){
				auto newNode=split(node);
				return std::make_pair(newNode,newNode->keys[0]);
			}
			return std::make_pair(nullptr,"");
		}else{
			size_t pos=node->findPos(key);
			if(pos==node->keys.size())pos=node->keys.size()-1;
			else if(pos>0&&key<node->keys[pos])pos--;
			
			auto res=insInt(node->kids[pos],key,val);
			auto newKid=res.first;
			auto promKey=res.second;
			
			if(newKid){
				node->keys.insert(node->keys.begin()+pos+1,promKey);
				node->kids.insert(node->kids.begin()+pos+1,newKid);
				
				if(node->keys.size()>=ord){
					auto newNode=split(node);
					std::string midKey=node->keys.back();
					node->keys.pop_back();
					return std::make_pair(newNode,midKey);
				}
			}
			return std::make_pair(nullptr,"");
		}
	}
	
public:
	BTree(size_t treeOrd=CFG::B_ORD)
		:root(std::make_shared<BNode>(true)),ord(treeOrd){}
	
	void insert(const std::string& key,uint64_t pid){
		auto res=insInt(root,key,pid);
		auto newNode=res.first;
		auto promKey=res.second;
		
		if(newNode){
			// root split
			auto newRoot=std::make_shared<BNode>(false);
			newRoot->keys.push_back(promKey);
			newRoot->kids.push_back(root);
			newRoot->kids.push_back(newNode);
			root=newRoot;
		}
	}
	
	uint64_t search(const std::string& key) const{
		auto node=root;
		
		while(!node->leaf){
			size_t pos=node->findPos(key);
			if(pos==node->keys.size())pos=node->keys.size()-1;
			else if(pos>0&&key<node->keys[pos])pos--;
			node=node->kids[pos];
		}
		
		size_t pos=node->findPos(key);
		if(pos<node->keys.size()&&node->keys[pos]==key){
			return node->vals[pos];
		}
		return 0; // not found
	}
	
	void remove(const std::string& key){
		// simple tombstone
		auto node=root;
		
		while(!node->leaf){
			size_t pos=node->findPos(key);
			if(pos==node->keys.size())pos=node->keys.size()-1;
			else if(pos>0&&key<node->keys[pos])pos--;
			node=node->kids[pos];
		}
		
		size_t pos=node->findPos(key);
		if(pos<node->keys.size()&&node->keys[pos]==key){
			node->vals[pos]=0;
		}
	}
	
	std::vector<std::string> getAllKeys() const{
		std::vector<std::string> res;
		auto node=root;
		
		while(!node->leaf&&!node->kids.empty()){
			node=node->kids[0];
		}
		
		while(node){
			for(const auto& key:node->keys){
				res.push_back(key);
			}
			node=node->next;
		}
		return res;
	}
};

// the boss
class SEng{
private:
	std::fstream dFile;
	BPool bp;
	BTree idx;
	JMan jrnl;
	uint64_t nextPid;
	
	std::shared_ptr<Pg> loadPg(uint64_t pid){
		auto cached=bp.get(pid);
		if(cached)return cached;
		
		auto pg=std::make_shared<Pg>(pid);
		dFile.seekg(pid*CFG::P_SZ);
		dFile.read(pg->data,CFG::P_SZ);
		
		bp.put(pid,pg);
		return pg;
	}
	
	void flushPg(std::shared_ptr<Pg> pg){
		dFile.seekp(pg->pid*CFG::P_SZ);
		dFile.write(pg->data,CFG::P_SZ);
		dFile.flush();
		pg->drty=false;
	}
	
	uint64_t allocPg(){
		return nextPid++;
	}
	
public:
	SEng():nextPid(1){
		dFile.open(CFG::D_FILE,
			std::ios::in|std::ios::out|std::ios::binary);
		
		if(!dFile.is_open()){
			dFile.open(CFG::D_FILE,
				std::ios::out|std::ios::binary);
			dFile.close();
			dFile.open(CFG::D_FILE,
				std::ios::in|std::ios::out|std::ios::binary);
		}
		
		dFile.seekg(0,std::ios::end);
		size_t fSz=dFile.tellg();
		nextPid=(fSz/CFG::P_SZ)+1;
	}
	
	~SEng(){
		flushAll();
		if(dFile.is_open()){
			dFile.close();
		}
	}
	
	bool insert(const std::string& key,const std::string& val){
		// check if it's already there
		if(idx.search(key)!=0){
			return false;
		}
		
		jrnl.logOp("INSERT",key,val);
		
		uint64_t pid=allocPg();
		Rec rec(key,val,pid);
		
		auto pg=std::make_shared<Pg>(pid);
		pg->wRec(rec);
		
		bp.put(pid,pg);
		flushPg(pg);
		
		idx.insert(key,pid);
		
		jrnl.commit();
		return true;
	}
	
	std::pair<bool,std::string> get(const std::string& key){
		uint64_t pid=idx.search(key);
		if(pid==0){
			return {false,""};
		}
		
		auto pg=loadPg(pid);
		Rec rec=pg->rRec();
		
		if(rec.del){
			return {false,""};
		}
		
		return {true,rec.getV()};
	}
	
	bool update(const std::string& key,const std::string& newVal){
		uint64_t pid=idx.search(key);
		if(pid==0){
			return false;
		}
		
		jrnl.logOp("UPDATE",key,newVal,pid);
		
		auto pg=loadPg(pid);
		Rec rec=pg->rRec();
		
		if(rec.del){
			return false;
		}
		
		strncpy(rec.val,newVal.c_str(),CFG::V_SZ-1);
		pg->wRec(rec);
		flushPg(pg);
		
		jrnl.commit();
		return true;
	}
	
	bool remove(const std::string& key){
		uint64_t pid=idx.search(key);
		if(pid==0){
			return false;
		}
		
		jrnl.logOp("DELETE",key,"",pid);
		
		auto pg=loadPg(pid);
		Rec rec=pg->rRec();
		rec.del=true;
		
		pg->wRec(rec);
		flushPg(pg);
		
		idx.remove(key);
		
		jrnl.commit();
		return true;
	}
	
	void flushAll(){
		auto dirty=bp.getDirty();
		for(auto& pg:dirty){
			flushPg(pg);
		}
		jrnl.trunc();
	}
	
	// slow way for benchmark
	std::pair<bool,std::string> lSearch(const std::string& key){
		dFile.seekg(0,std::ios::end);
		size_t fSz=dFile.tellg();
		size_t numPgs=fSz/CFG::P_SZ;
		
		for(uint64_t pid=1;pid<=numPgs;++pid){
			dFile.seekg(pid*CFG::P_SZ);
			char buf[CFG::P_SZ];
			dFile.read(buf,CFG::P_SZ);
			
			Rec rec;
			memcpy(&rec,buf,sizeof(Rec));
			
			if(!rec.del&&rec.getK()==key){
				return {true,rec.getV()};
			}
		}
		return {false,""};
	}
	
	void stats(){
		dFile.seekg(0,std::ios::end);
		size_t fSz=dFile.tellg();
		size_t numPgs=fSz/CFG::P_SZ;
		
		std::cout<<"=== Database Statistics ==="<<std::endl;
		std::cout<<"File size: "<<fSz<<" bytes"<<std::endl;
		std::cout<<"Number of pages: "<<numPgs<<std::endl;
		std::cout<<"Page size: "<<CFG::P_SZ<<" bytes"<<std::endl;
		std::cout<<"Cache size: "<<CFG::C_SZ<<" pages"<<std::endl;
	}
};
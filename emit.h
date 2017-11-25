#ifndef EMIT_H
#define EMIT_H

#include <stdint.h>
#include <string.h>
#include <string>

class Import {
public:
  int argcount;
  bool isExternal;
  bool isVarArgs;
  const char* name;
  size_t namelen = 0;
  int offset;
};


class Assembly {
public:
  unsigned char* bytecode;
  size_t capacity;
  size_t len;
  Assembly(Import* imports, int importLen) {
    bytecode = new unsigned char[24];
    capacity = 24;
    len = 0;
    write(importLen);
    for(size_t i = 0;i<importLen;i++) {
      write(imports[i].argcount);
      write(imports[i].isExternal);
      write(imports[i].isVarArgs);
      if(!imports[i].isExternal) {
	write(imports[i].offset);
      }
      if(imports[i].namelen) {
	write(imports[i].name,imports[i].namelen);
	unsigned char terminator = 0;
	write(terminator);
      }else {
	writeString(imports[i].name);
      }
      
    }
  }
  Assembly():Assembly(0,0) {
    
  }
  void push(const void* data, int len) {
    unsigned char opcode = 0;
    write(opcode);
    write(len);
    write(data,len);
  }
  void pop() {
    unsigned char opcode = 1;
    write(opcode);
  }
  void load() {
    unsigned char opcode = 2;
    write(opcode);
  }
  void store() {
    unsigned char opcode = 3;
    write(opcode);
  }
  void branch(int offset) {
    unsigned char opcode = 4;
    write(opcode);
    write(offset);
  }
  void call(int funcid) {
    unsigned char opcode = 5;
    write(opcode);
    write(funcid);
  }
  void ret() {
    unsigned char opcode = 6;
    write(opcode);
  }
  void setrsp() {
    unsigned char opcode = 7;
    write(opcode);
  }
  void getrsp() {
    unsigned char opcode = 8;
    write(opcode);
  }
  
  
  void writeString(const char* value) {
    while(*value) {
      write(*value);
      value++;
    }
    write(*value);
  }
  void write(const void* data, size_t len) {
    size_t oldlen = capacity;
    while(this->len+len>capacity) {
      capacity*=2;
    }
    if(capacity != oldlen) {
      unsigned char* newcode = new unsigned char[capacity];
      memcpy(newcode,bytecode,this->len);
      delete[] bytecode;
      bytecode = newcode;
    }
    memcpy(bytecode+this->len,data,len);
    this->len+=len;
  }
  
  template<typename T>
  void write(const T& value) {
    write((unsigned char*)&value,sizeof(T));
  }
  
  
  
};

#endif
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <vector>
#include <string.h>
#include <string>
#include <map>
#include <dlfcn.h>
#include "emit.h"

//UVM Interpreter and Test Suite

class StackFrame {
public:
  unsigned char* ptr;
  size_t size;
  StackFrame(size_t sz) {
    size= sz;
    ptr = new unsigned char[sz];
  }
  ~StackFrame() {
    delete[] ptr;
  }
};

class FunctionInformation {
public:
  int argcount;
  int offset;
  bool isExternal;
  bool isVarArgs;
  int outsize;
  const char* name;
  void* ptr;
};


class VM;
void platform_call_wrapper(void* ptr, StackFrame** arguments, size_t argcount, size_t outsize,bool varargs, VM* vm);
void vm_init(VM* vm);
class VM {
public:
  unsigned char* firmware;
  unsigned char* cip;
  unsigned char* rsp;
  size_t* heap;
  std::vector<StackFrame*> stack;
  std::vector<unsigned char*> retstack;
  FunctionInformation* imports;
  std::map<std::string,void*> overrides;
  void addOverride(const std::string& name, void* ptr) {
    overrides[name] = ptr;
  }
  VM(unsigned char* firmware) {
    vm_init(this);
    this->firmware = firmware;
    cip = firmware;
    size_t targetHeapSize = 1024*1024*5;
    heap = new size_t[targetHeapSize/sizeof(size_t)];
    
    
    int imports_size;
    read(imports_size);
    imports = new FunctionInformation[imports_size];
    for(size_t i = 0;i<imports_size;i++) {
      read(imports[i].argcount);
      read(imports[i].isExternal);
      read(imports[i].isVarArgs);
      read(imports[i].outsize);
      if(imports[i].outsize == -1) {
	imports[i].outsize = sizeof(void*);
      }
      if(!imports[i].isExternal) {
	read(imports[i].offset);
      }
      imports[i].name = readString();
      if(imports[i].isExternal) {
	if(overrides.find(imports[i].name) != overrides.end()) {
	  imports[i].ptr = overrides[imports[i].name];
	}else {
	imports[i].ptr = dlsym(0,imports[i].name);
	}
	if(!imports[i].ptr) {
	  printf("UNABLE TO RESOLVE SYMBOL %s\n",imports[i].name);
	}
      }
    }
    rsp = (unsigned char*)heap;
  }
  template<typename T>
  void read(T& out) {
    memcpy(&out,cip,sizeof(T));
    cip+=sizeof(T);
  }
  void push(void* data, size_t size) {
    StackFrame* frame = new StackFrame(size);
    memcpy(frame->ptr,data,size);
    stack.push_back(frame);
  }
  template<typename T>
  void pop(T& out) {
    StackFrame* frame = stack.back();
    stack.pop_back();
    memcpy(&out,frame->ptr,sizeof(T));
    delete frame;
  }
  void pop() {
    StackFrame* frame = stack.back();
    stack.pop_back();
    delete frame;
  }
  const char* readString() {
    const char* mander = (const char*)cip;
    while(*cip){cip++;}
    cip++;
    return mander;
  }
  void exec() {
    while(1) {
    unsigned char opcode;
    unsigned char* oldip = cip;
    read(opcode);
    switch(opcode) {
    case 0:
    {
      //PUSH constant
      int len;
      read(len);
      push(cip,len);
      cip+=len;
    }
      break;
    case 1:
    {
      //POP
      pop();
    }
      break;
    case 2:
    {
      //Load
      void* addr;
      size_t sz;
      pop(sz);
      pop(addr);
      push(addr,sz);
    }
      break;
    case 3:
    {
      //Store
      StackFrame* value = stack.back();
      void* addr;
      memcpy(addr,value->ptr,value->size);
      pop(addr);
      pop();
    }
      break;
    case 4:
    {
      //CIP-relative branch
      int offset;
      pop(offset);
      bool shouldBranch = false;
      StackFrame* frame = stack.back();
      for(size_t i = 0;i<frame->size;i++) {
	if(frame->ptr[i]) {
	  shouldBranch = true;
	}
      }
      pop();
      if(shouldBranch) {
	cip =oldip+offset;
      }
    }
      break;
    case 5:
    {
      //Procedure call
      int funcid;
      read(funcid);
      FunctionInformation* info = imports+funcid;
      size_t argcount = info->argcount;
      if(info->isExternal) {
      StackFrame** args = new StackFrame*[argcount+1];
      for(size_t i = 0;i<argcount;i++) {
	args[i] = stack.back();
	stack.pop_back();
      }
      
      platform_call_wrapper(info->ptr,args,argcount,info->outsize,info->isVarArgs,this);
      for(size_t i = 0;i<argcount;i++) {
	delete args[i];
      }
      delete[] args;
      }else {
	retstack.push_back(cip);
	cip = firmware+info->offset;
      }
    }
      break;
    case 6:
    {
      //Procedure call return/exit
      if(!retstack.size()) {
	return;
      }
      cip = retstack.back();
      retstack.pop_back();
    }
      break;
    case 7:
    {
      //Set stack pointer
      pop(rsp);
    }
      break;
    case 8:
    {
      //Read stack pointer
      push(&rsp,sizeof(rsp));
    }
      break;
  }
    }
  }
};



unsigned char encode_modrm(unsigned char rm, unsigned char reg, unsigned char mod) {
  return rm | (reg << 3) | (mod << 6);
}

enum X86Register {
  RAX,
  RCX,
  RDX,
  RBX, //preserved
  RSP, //preserved
  RBP, //preserved
  RSI,
  RDI
};

class ASMEmit {
public:
  int dzero;
  unsigned char* code;
  unsigned char* ptr;
  ASMEmit() {
    dzero = open("/dev/zero",O_RDWR);
  code = (unsigned char*)mmap(0,4096,PROT_READ | PROT_WRITE | PROT_EXEC,MAP_PRIVATE,dzero,0);
    int rdir = mprotect(code,4096,PROT_READ | PROT_WRITE | PROT_EXEC);
    ptr = code;
  }
  void write(unsigned char byte) {
    *ptr = byte;
    ptr++;
  }
  template<typename T>
  void writelong(const T& value) {
    memcpy(ptr,&value,sizeof(T));
    ptr+=sizeof(T);
  }
  void rpush64(X86Register reg) {
    write(0x48);
    write(0x50+reg);
  }
  void rpop64(X86Register reg) {
    write(0x48);
    write(0x58+reg);
  }
  void irmov64(int64_t value, X86Register dest) {
    write(0x48);
    write(0xB8+dest);
    writelong(value);
  }
  void rrmov64(X86Register dest, X86Register src) {
    write(0x48); //64-bit instruction
    write(0x89); //mov
    write(encode_modrm(src,dest,0b11));
  }
  void mcall(X86Register address) {
    write(0xff);
    write(encode_modrm(address,2,0b11));
  }
  void ret() {
    write(0xc3);
  }
  ~ASMEmit() {
    munmap(code,4096);
    close(dzero);
  }
};

thread_local ASMEmit emitter;


size_t x86_call_wrapper(void* ptr, int64_t* arguments, size_t argcount,size_t outsize, bool varargs) {
  X86Register regmap[] = {
    RDI, RSI, RDX, RCX
  };
  emitter.ptr = emitter.code;
  int regno = 0;
  if(varargs) {
    emitter.irmov64(0,RAX); //varargs needs a 0 stored in RAX register to indicate no special registers are in use.
  }
  emitter.rpush64(RBX);
  emitter.irmov64((int64_t)ptr,RBX);
  for(size_t i = 0;i<argcount;i++) {
    emitter.irmov64(arguments[i],regmap[(regno+argcount)-i-1]);
  }
  regno+=argcount;
  emitter.mcall(RBX);
  emitter.rpop64(RBX);
  emitter.ret();
  unsigned char* code = emitter.code;
  return ((size_t(*)())code)();
}


void platform_call_wrapper(void* ptr, StackFrame** arguments, size_t argcount, size_t outsize, bool varargs, VM* vm) {
  int64_t* args = new int64_t[argcount];
  for(size_t i = 0;i<argcount;i++) {
    memcpy(args+i,arguments[i]->ptr,arguments[i]->size);
  }
  size_t retval = x86_call_wrapper(ptr,args,argcount,outsize,varargs);
  if(outsize) {
    vm->push(&retval,outsize);
  }
  delete[] args;
}


extern "C" {
void nativefunc(const char* somearg) {
  printf("This is a native function called from UVM! The string is %s\n",somearg);
}
size_t __uvm_intrinsic_ptradd(size_t a, size_t b) {
  return a+b;
}
size_t x86_sub(size_t a, size_t b) {
  return a-b;
}
size_t x86_mul(size_t a, size_t b) {
  return a*b;
}
size_t x86_div(size_t a, size_t b) {
  return a*b;
}
//thisptr call -- thisptr is always passed as last argument
void x86_assign_int(int val, int* dest) {
  printf("Assign %i to %p\n",val,dest);
  *dest = val;
}
void print(int value) {
  printf("%i\n",value);
}

}


void vm_init(VM* vm) {
  vm->addOverride("global\\int\\+\\",(void*)&__uvm_intrinsic_ptradd);
  vm->addOverride("global\\int\\-\\",(void*)&x86_sub);
  vm->addOverride("global\\int\\*\\",(void*)&x86_mul);
  vm->addOverride("global\\int\\/\\",(void*)&x86_div);
  vm->addOverride("global\\int\\=\\",(void*)&x86_assign_int);
  vm->addOverride("global\\print\\",(void*)&print);
}


static void uvm_exec(unsigned char* bytecode) {
  VM vm(bytecode);
  vm.exec();
}

static void uvm_testprog() {
  Import ant[2];
  ant[0].name = "printf";
  ant[1].name = "nativefunc";
  ant[0].argcount = 2;
  ant[0].isVarArgs = true;
  ant[1].isVarArgs = false;
  ant[1].argcount = 1;
  ant[0].isExternal = 1;
  ant[1].isExternal = 1;
  Assembly code(ant,2);
  const char* othertext = "Cool demo";
  code.push(&othertext,sizeof(othertext));
  code.call(1);
  const char* txt = "Hello world! The answer to life, the universe, and everything is %i\n";
  int eger = 42;
  code.push(&txt,sizeof(txt));
  code.push(&eger,sizeof(eger));
  code.call(0);
  code.ret();
  uvm_exec(code.bytecode);
}

//5
int main(int argc, char** argv) {

  //uvm_testprog();



  
  int fd = open(argv[1],O_RDONLY);
  struct stat us; //MAC == Status symbol
  fstat(fd,&us);
  
  unsigned char* mander =(unsigned char*)mmap(0,us.st_size,PROT_READ,MAP_SHARED,fd,0);
  uvm_exec(mander);
  return 0;
}

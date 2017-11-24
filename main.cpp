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
  const char* name;
};

class VM {
public:
  unsigned char* firmware;
  unsigned char* cip;
  size_t* heap;
  std::vector<StackFrame*> stack;
  FunctionInformation* imports;
  void platform_call_wrapper(const char* name, StackFrame** arguments, size_t argcount) {
    
  }
  
  
  VM(unsigned char* firmware) {
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
      read(imports[i].offset);
      imports[i].name = (char*)cip;
      while(*cip){cip++;};
    }
  }
  template<typename T>
  void read(T& out) {
    memcpy(&out,cip,sizeof(T));
    cip+=sizeof(T);
  }
  void push(void* data, size_t size) {
    StackFrame* frame = new StackFrame(size);
    memcpy(frame->ptr,data,size);
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
      pop(addr);
      pop(sz);
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
      //External call
      int _argcount;
      pop(_argcount);
      size_t argcount = _argcount;
      StackFrame** args = new StackFrame*[argcount];
      for(size_t i = 0;i<argcount;i++) {
	args[i] = stack.back();
	stack.pop_back();
      }
      const char* funcname;
      pop(funcname);
      platform_call_wrapper(funcname,args,argcount);
      for(size_t i = 0;i<argcount;i++) {
	delete args[i];
      }
      delete[] args;
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
  RBX,
  RSP,
  RBP,
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
  void rrmov64(X86Register src, X86Register dest) {
    write(0x48); //64-bit instruction
    write(0x89); //mov
    write(encode_modrm(src,dest,0b11));
  }
  void ret() {
    write(0xc3);
  }
  ~ASMEmit() {
    munmap(code,4096);
    close(dzero);
  }
};

static void uvm_exec(unsigned char* bytecode) {
  VM vm(bytecode);
  vm.exec();
}

//5
int main(int argc, char** argv) {
  ASMEmit ter;
int64_t m = 5;
void* funcptr = dlsym(0,"printf");
ter.rrmov64(RDI,RAX);
ter.ret();
((void(*)(int64_t))ter.code)(m);

  
  int fd = open(argv[1],O_RDONLY);
  struct stat us; //MAC == Status symbol
  fstat(fd,&us);
  
  unsigned char* mander =(unsigned char*)mmap(0,us.st_size,PROT_READ,MAP_SHARED,fd,0);
  uvm_exec(mander);
  return 0;
}

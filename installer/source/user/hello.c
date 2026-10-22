typedef unsigned long long u64;
enum { SYS_Exit=0, SYS_Write=1, SYS_Alloc=3 };
static u64 sy3(u64 n,u64 a,u64 b,u64 c){u64 r;__asm__ volatile("mov %[n],%%rax\nmov %[a],%%rdi\nmov %[b],%%rsi\nmov %[c],%%rdx\nint $0x80\nmov %%rax,%[r]":[r]"=r"(r):[n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c):"rax","rdi","rsi","rdx","memory","cc");return r;}
static void puts(const char *s){u64 n=0;while(s[n])n++;sy3(SYS_Write,1,(u64)s,n);}
void hello_main(void){
  puts("hello: started by smss\n");
  u64 p=sy3(SYS_Alloc,0,4096,0);
  if(p){char *b=(char*)p;b[0]='h';b[1]='i';b[2]='\n';b[3]=0;puts(b);puts("hello: NtAllocateVirtualMemory ok\n");}
  else puts("hello: alloc failed\n");
  puts("hello: exiting\n");
  sy3(SYS_Exit,0,0,0);
  for(;;)__asm__ volatile("hlt");
}

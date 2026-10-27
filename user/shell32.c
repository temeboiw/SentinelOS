typedef unsigned short WCHAR;
typedef unsigned int DWORD;
typedef unsigned int UINT;
typedef void *HLOCAL;
extern HLOCAL LocalAlloc(UINT flags,unsigned long long bytes);
extern HLOCAL LocalFree(HLOCAL memory);
__attribute__((dllexport)) WCHAR **CommandLineToArgvW(const WCHAR *cmdline,int *argc_out) {
 if(argc_out)*argc_out=0;
 if(!cmdline){cmdline=(const WCHAR *)GetCommandLineW();}
 if(!cmdline)return 0;
 unsigned long long len=0;while(cmdline[len])len++;
 unsigned long long argc=0;int quoted=0;unsigned long long i=0;
 while(i<len){while(cmdline[i]==' '||cmdline[i]=='\t')i++;if(i>=len)break;argc++;quoted=0;
  while(i<len){if(cmdline[i]=='"' ){quoted=!quoted;i++;continue;}if(!quoted&&(cmdline[i]==' '||cmdline[i]=='\t'))break;
   if(cmdline[i]=='\\'){while(i<len&&cmdline[i]=='\\')i++;if(i<len&&cmdline[i]=='"'){i++;quoted=!quoted;}else if(i<len&& (quoted||(cmdline[i]!=' '&&cmdline[i]!='\t')))i++;continue;}
   i++;
  }
 }
 unsigned long long ptrbytes=(argc+1)*sizeof(WCHAR *),textbytes=(len+1)*sizeof(WCHAR);
 unsigned long long total=ptrbytes+textbytes+16;
 WCHAR **argv=(WCHAR **)LocalAlloc(0x0040u,total);
 if(!argv)return 0;
 WCHAR *copy=(WCHAR *)((unsigned char *)argv+ptrbytes);for(i=0;i<=len;i++)copy[i]=cmdline[i];
 unsigned long long pos=0,narg=0;
 while(pos<len){while(copy[pos]==' '||copy[pos]=='\t')pos++;if(pos>=len)break;argv[narg++]=copy+pos;quoted=0;unsigned long long write=pos;
  while(pos<len){WCHAR ch=copy[pos];if(ch=='\\'){unsigned long long bs=0;while(pos<len&&copy[pos]=='\\'){bs++;pos++;}if(pos<len&&copy[pos]=='"'){for(unsigned long long z=0;z<bs/2;z++)copy[write++]='\\';if(bs&1){copy[write++]='"';pos++;}else{quoted=!quoted;pos++;}}else{for(unsigned long long z=0;z<bs;z++)copy[write++]='\\';}continue;}
   if(ch=='"'){quoted=!quoted;pos++;continue;}if(!quoted&&(ch==' '||ch=='\t'))break;copy[write++]=ch;pos++;
  }
  copy[write]=0;pos++;
 }
 argv[narg]=0;if(argc_out)*argc_out=(int)narg;return argv;
}

/* Conservative MSI execution subset.
 * Unknown conditions/actions fail before disk mutation. This is not a full
 * Windows Installer implementation; it deliberately refuses unsupported
 * registry, service, shortcut, feature-selection and native custom actions. */

typedef struct {
    char id[64], parent[64], defdir[256], path[512];
    int state;
} MsiDirPath;
typedef struct {
    char id[64], raw_name[240], name[240], target[512];
    u32 size;
    long long sequence;
} MsiFilePlan;
typedef struct { char name[64], value[256]; } MsiRuntimeProperty;
typedef struct { char id[64], path[512]; } MsiDirOverride;

#define MSI_MAX_DIRS 512u
#define MSI_MAX_FILE_PLANS 4096u
static MsiDirPath msi_dir_paths[MSI_MAX_DIRS];
static MsiFilePlan msi_file_plans[MSI_MAX_FILE_PLANS];
static u32 msi_dir_count, msi_file_plan_count;
static MsiSequenceRow msi_install_rows[512];
static u8 msi_install_enabled[512];
static u8 msi_copy_buffer[65536];
static MsiRuntimeProperty msi_runtime_props[128];
static MsiDirOverride msi_dir_overrides[128];
static u32 msi_runtime_prop_count, msi_dir_override_count;
static u8 msi_custom_preexecuted[512];
static int msi_resolve_dir(u32 idx,u32 depth);

static int msi_cell_text(const char *table,u32 row,u32 col,char *out_text,u32 cap) {
    int is_text=0;
    if (!msi_table_cell(table,row,col,out_text,cap,0,&is_text) || !is_text) {
        if (cap) out_text[0]=0;
        return 0;
    }
    return 1;
}
static int msi_property_value(const char *key,char *out_value,u32 cap) {
    MsiTable *t=msi_find_table("Property");
    if (cap) out_value[0]=0;
    for(u32 i=0;i<msi_runtime_prop_count;i++) if(same_name(msi_runtime_props[i].name,key)) {
        u32 j=0; while(msi_runtime_props[i].value[j] && j+1<cap){out_value[j]=msi_runtime_props[i].value[j];j++;}
        if(cap) out_value[j]=0; return 1;
    }
    for(u32 i=0;i<msi_dir_count;i++) if(same_name(msi_dir_paths[i].id,key)) {
        u32 j=0; while(msi_dir_paths[i].path[j] && j+1<cap){out_value[j]=msi_dir_paths[i].path[j];j++;}
        if(cap) out_value[j]=0; return 1;
    }
    if (!t) return 0;
    for(u32 i=0;i<t->rows;i++) {
        char k[128],v[256];
        if(msi_cell_text("Property",i,0,k,sizeof k) &&
           msi_cell_text("Property",i,1,v,sizeof v) && same_name(k,key)) {
            u32 j=0; while(v[j] && j+1<cap){out_value[j]=v[j];j++;}
            if(cap) out_value[j]=0;
            return 1;
        }
    }
    return 0;
}
static void msi_trim(char *s) {
    u32 n=0; while(s[n]) n++;
    u32 a=0; while(a<n && (s[a]==' '||s[a]=='\t')) a++;
    while(n>a && (s[n-1]==' '||s[n-1]=='\t')) n--;
    if(a) { u32 j=0; while(a<n) s[j++]=s[a++]; s[j]=0; }
    else s[n]=0;
}
static int msi_truthy(const char *s) {
    return s && s[0] && !same_name(s,"0") && !same_name(s,"false");
}
/* Returns -1 for unsupported syntax; caller must abort before mutation. */
static int msi_condition_eval(const char *condition) {
    char c[128];
    u32 n=0; while(condition && condition[n] && n+1<sizeof c){c[n]=condition[n];n++;}
    if(condition && condition[n]) return -1;
    c[n]=0; msi_trim(c);
    n=0; while(c[n]) n++;
    if(!c[0] || same_name(c,"1") || same_name(c,"TRUE")) return 1;
    if(same_name(c,"0") || same_name(c,"FALSE") || same_name(c,"Installed")) return 0;
    if(same_name(c,"NOT Installed")) return 1;
    if(c[0]=='(' && n>2 && c[n-1]==')') { c[n-1]=0; for(u32 i=0;i<n-2;i++)c[i]=c[i+1]; c[n-2]=0; msi_trim(c); n=0; while(c[n]) n++; }

    int neg=0;
    if(c[0]=='N' && c[1]=='O' && c[2]=='T' && c[3]==' ') {
        if(n<4) return -1;
        neg=1; for(u32 i=0;c[i+4];i++)c[i]=c[i+4]; c[n-4]=0; msi_trim(c);
        n=0; while(c[n]) n++;
    }
    char *op=0; u32 oplen=0;
    for(u32 i=0;c[i];i++) {
        if(c[i]=='~'||c[i]=='&'||c[i]=='|'||c[i]=='!') return -1;
        if(c[i]=='=' && c[i+1]!='>') { op=c+i; oplen=1; break; }
        if(c[i]=='<' && c[i+1]=='>') { op=c+i; oplen=2; break; }
        if(c[i]=='>' && c[i+1]=='=') return -1;
        if(c[i]=='<' && c[i+1]=='=') return -1;
        if(c[i]=='>' || c[i]=='<') return -1;
    }
    if(op) {
        char lhs[64],rhs[96]; u32 ln=(u32)(op-c), ri=0;
        if(!ln || ln>=sizeof lhs) return -1;
        for(u32 i=0;i<ln;i++) lhs[i]=c[i]; lhs[ln]=0; msi_trim(lhs);
        const char *r=op+oplen; while(*r==' '||*r=='\t')r++;
        while(r[ri] && ri+1<sizeof rhs){rhs[ri]=r[ri];ri++;}
        if(r[ri]) return -1; rhs[ri]=0; msi_trim(rhs);
        if(!lhs[0] || !rhs[0]) return -1;
        if((rhs[0]=='\'' && ri>=2 && rhs[ri-1]=='\'') ||
           (rhs[0]=='"' && ri>=2 && rhs[ri-1]=='"')) {
            for(u32 i=0;i<ri-2;i++) rhs[i]=rhs[i+1];
            rhs[ri-2]=0;
        }
        char value[256]; (void)msi_property_value(lhs,value,sizeof value);
        int eq=same_name(value,rhs);
        int result=oplen==1 ? eq : !eq;
        return neg ? !result : result;
    }
    if(!c[0]) return neg?0:1;
    for(u32 i=0;c[i];i++) if(c[i]==' '||c[i]=='\t'||c[i]=='('||c[i]==')') return -1;
    char value[256]; (void)msi_property_value(c,value,sizeof value);
    int v=msi_truthy(value);
    return neg?!v:v;
}
static int msi_action_supported(const char *a) {
    static const char *const supported[]={
        "CostInitialize","FileCost","CostFinalize","InstallValidate",
        "InstallInitialize","InstallFiles","InstallFinalize","ResolveSource"
    };
    for(u32 i=0;i<sizeof supported/sizeof supported[0];i++)
        if(same_name(a,supported[i])) return 1;
    return 0;
}
static int msi_custom_action_kind(const char *action) {
    MsiTable *t=msi_find_table("CustomAction");
    if(!t) return 0;
    for(u32 i=0;i<t->rows;i++) {
        char name[96]; long long type=0;
        if(!msi_cell_text("CustomAction",i,0,name,sizeof name) ||
           !msi_table_cell("CustomAction",i,1,0,0,&type,0)) return 0;
        if(!same_name(name,action)) continue;
        if(type==19) return 19; /* Error */
        if(type==35) return 35; /* SetDirectory */
        if(type==51) return 51; /* SetProperty */
        return 0; /* never run native DLL, EXE, script or deferred actions */
    }
    return 0;
}
static int msi_set_runtime_property(const char *name,const char *value) {
    if(!name || !name[0] || !value) return 0;
    u32 i=0; while(name[i] && i+1<sizeof msi_runtime_props[0].name)i++;
    if(name[i]) return 0;
    u32 j=0; while(value[j] && j+1<sizeof msi_runtime_props[0].value)j++;
    if(value[j]) return 0;
    int slot=-1;
    for(u32 k=0;k<msi_runtime_prop_count;k++) if(same_name(msi_runtime_props[k].name,name)){slot=(int)k;break;}
    if(slot<0) { if(msi_runtime_prop_count>=128)return 0; slot=(int)msi_runtime_prop_count++; }
    for(u32 k=0;k<=i;k++)msi_runtime_props[slot].name[k]=name[k];
    for(u32 k=0;k<=j;k++)msi_runtime_props[slot].value[k]=value[k];
    return 1;
}
static int msi_format_value(const char *src,char *dst,u32 cap) {
    u32 outn=0;
    for(u32 i=0;src[i];) {
        if(src[i]=='[') {
            u32 end=i+1; while(src[end] && src[end]!=']')end++;
            if(!src[end] || end==i+1) return 0;
            char key[64]; u32 n=end-i-1;
            if(n>=sizeof key)return 0;
            for(u32 j=0;j<n;j++)key[j]=src[i+1+j];key[n]=0;
            char val[256]; (void)msi_property_value(key,val,sizeof val);
            for(u32 j=0;val[j];j++){if(outn+1>=cap)return 0;dst[outn++]=val[j];}
            i=end+1;
        } else { if(outn+1>=cap)return 0; dst[outn++]=src[i++]; }
    }
    if(!cap)return 0;dst[outn]=0;return 1;
}
static int msi_find_dir_override(const char *id) {
    for(u32 i=0;i<msi_dir_override_count;i++) if(same_name(msi_dir_overrides[i].id,id))return (int)i;
    return -1;
}
static int msi_set_dir_override(const char *id,const char *path) {
    if(!id || !id[0] || !path || path[0]!='C' || path[1]!=':' || path[2]!='\\') return 0;
    u32 n=0;while(path[n] && n+1<512)n++;if(path[n])return 0;
    for(u32 i=0;i<n;i++) if(path[i]=='['||path[i]==']')return 0;
    for(u32 i=3;i<n;) {
        while(i<n && (path[i]=='\\'||path[i]=='/'))i++;
        u32 start=i; while(i<n && path[i]!='\\' && path[i]!='/')i++;
        if(i-start==2 && path[start]=='.' && path[start+1]=='.')return 0;
    }
    int slot=msi_find_dir_override(id);
    if(slot<0){if(msi_dir_override_count>=128)return 0;slot=(int)msi_dir_override_count++;}
    u32 j=0;while(id[j] && j+1<sizeof msi_dir_overrides[0].id){msi_dir_overrides[slot].id[j]=id[j];j++;}
    if(id[j])return 0;msi_dir_overrides[slot].id[j]=0;
    for(u32 k=0;k<=n;k++)msi_dir_overrides[slot].path[k]=path[k];
    for(u32 k=0;k<msi_dir_count;k++)msi_dir_paths[k].state=0;
    for(u32 k=0;k<msi_dir_count;k++)if(!msi_resolve_dir(k,0))return 0;
    (void)msi_set_runtime_property(id,path);
    return 1;
}
static int msi_execute_custom_action(const char *action) {
    MsiTable *t=msi_find_table("CustomAction");
    if(!t)return 0;
    for(u32 i=0;i<t->rows;i++) {
        char name[96],source[128],target[256]; long long type=0;
        if(!msi_cell_text("CustomAction",i,0,name,sizeof name) ||
           !msi_table_cell("CustomAction",i,1,0,0,&type,0)) return 0;
        if(!same_name(name,action))continue;
        source[0]=target[0]=0;
        if(!msi_cell_text("CustomAction",i,2,source,sizeof source) ||
           !msi_cell_text("CustomAction",i,3,target,sizeof target))return 0;
        if(type==19) {
            char message[256];
            if(!msi_format_value(target,message,sizeof message))return 0;
            out("MSI Error custom action: ");out(message);out("\n");return 0;
        }
        if(type==51) {
            char value[256];
            if(!msi_format_value(target,value,sizeof value))return 0;
            return msi_set_runtime_property(source,value);
        }
        if(type==35) {
            char path[512];
            if(!msi_format_value(target,path,sizeof path))return 0;
            return msi_set_dir_override(source,path);
        }
        return 0;
    }
    return 0;
}
static int msi_load_sequence(MsiSequenceRow *rows,u8 *enabled,u32 cap,u32 *count_out) {
    MsiTable *t=msi_find_table("InstallExecuteSequence");
    if(!t || !t->rows || t->rows>cap) return 0;
    u32 count=t->rows;
    for(u32 i=0;i<count;i++) {
        long long seq=0;
        rows[i].action[0]=rows[i].condition[0]=0;
        if(!msi_cell_text("InstallExecuteSequence",i,0,rows[i].action,sizeof rows[i].action) ||
           !msi_table_cell("InstallExecuteSequence",i,1,rows[i].condition,sizeof rows[i].condition,0,0) ||
           !msi_table_cell("InstallExecuteSequence",i,2,0,0,&seq,0)) return 0;
        int cond_text=0;
        if(!msi_table_cell("InstallExecuteSequence",i,1,rows[i].condition,sizeof rows[i].condition,0,&cond_text)) return 0;
        if(!cond_text) rows[i].condition[0]=0;
        rows[i].sequence=seq;
    }
    for(u32 i=1;i<count;i++) {
        MsiSequenceRow row=rows[i]; u32 j=i;
        while(j && rows[j-1].sequence>row.sequence){rows[j]=rows[j-1];--j;}
        rows[j]=row;
    }
    int init=-1, files=-1, final=-1;
    for(u32 i=0;i<count;i++) {
        enabled[i]=0;
        if(!rows[i].action[0] || rows[i].sequence==0) continue;
        int cond=msi_condition_eval(rows[i].condition);
        if(cond<0) { out("Install blocked: unsupported condition: ");out(rows[i].condition);out("\n");return 0; }
        if(!msi_action_supported(rows[i].action) &&
           !msi_custom_action_kind(rows[i].action)) {
            out("Install blocked: unsupported MSI action/custom action: ");out(rows[i].action);out("\n");
            return 0;
        }
        enabled[i]=1;
        if(same_name(rows[i].action,"InstallInitialize")) { if(init>=0)return 0; init=(int)i; }
        if(same_name(rows[i].action,"InstallFiles")) { if(files>=0)return 0; files=(int)i; }
        if(same_name(rows[i].action,"InstallFinalize")) { if(final>=0)return 0; final=(int)i; }
    }
    if(init<0 || files<0 || final<0 || !(init<files && files<final)) return 0;
    *count_out=count;
    return 1;
}

static int msi_builtin_dir(const char *id,char *out_path,u32 cap) {
    const char *v=0;
    if(same_name(id,"TARGETDIR")) v="C:\\";
    else if(same_name(id,"ProgramFilesFolder")||same_name(id,"ProgramFiles64Folder")) v="C:\\Program Files";
    else if(same_name(id,"CommonFilesFolder")) v="C:\\Program Files\\Common Files";
    else if(same_name(id,"SystemFolder")) v="C:\\Windows\\System32";
    else if(same_name(id,"WindowsFolder")) v="C:\\Windows";
    else if(same_name(id,"TempFolder")) v="C:\\Windows\\Temp";
    else if(same_name(id,"CommonAppDataFolder")) v="C:\\ProgramData";
    else if(same_name(id,"AppDataFolder")) v="C:\\Users\\Public\\AppData\\Roaming";
    else if(same_name(id,"LocalAppDataFolder")) v="C:\\Users\\Public\\AppData\\Local";
    else if(same_name(id,"PersonalFolder")) v="C:\\Users\\Public\\Documents";
    else if(same_name(id,"DesktopFolder")) v="C:\\Users\\Public\\Desktop";
    else if(same_name(id,"ProgramMenuFolder")) v="C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs";
    if(!v) return 0;
    u32 i=0; while(v[i] && i+1<cap){out_path[i]=v[i];i++;}
    if(v[i]) return 0; out_path[i]=0; return 1;
}
static int msi_dir_index(const char *id) {
    for(u32 i=0;i<msi_dir_count;i++) if(same_name(msi_dir_paths[i].id,id)) return (int)i;
    return -1;
}
static int msi_resolve_dir(u32 idx,u32 depth) {
    if(idx>=msi_dir_count || depth>32) return 0;
    MsiDirPath *d=&msi_dir_paths[idx];
    if(d->state==2) return 1;
    if(d->state==1) return 0;
    int oi=msi_find_dir_override(d->id);
    if(oi>=0) {
        u32 j=0; while(msi_dir_overrides[oi].path[j] && j+1<sizeof d->path){d->path[j]=msi_dir_overrides[oi].path[j];j++;}
        if(msi_dir_overrides[oi].path[j]) return 0;
        d->path[j]=0; d->state=2; return 1;
    }
    if(msi_builtin_dir(d->id,d->path,sizeof d->path)){d->state=2;return 1;}
    d->state=1;
    char parent_path[512];
    if(!d->parent[0]) {
        if(!msi_builtin_dir("TARGETDIR",parent_path,sizeof parent_path)) return 0;
    } else {
        int pi=msi_dir_index(d->parent);
        if(pi>=0) {
            if(!msi_resolve_dir((u32)pi,depth+1)) return 0;
            u32 j=0; while(msi_dir_paths[pi].path[j] && j+1<sizeof parent_path){parent_path[j]=msi_dir_paths[pi].path[j];j++;}
            if(msi_dir_paths[pi].path[j]) return 0; parent_path[j]=0;
        } else if(!msi_builtin_dir(d->parent,parent_path,sizeof parent_path)) return 0;
    }
    char leaf[256]; u32 n=0;
    const char *s=d->defdir;
    for(u32 i=0;s[i];i++) if(s[i]==':') { s=d->defdir+i+1; break; }
    while(*s && n+1<sizeof leaf) leaf[n++]=*s++;
    if(*s) return 0; leaf[n]=0;
    char *pipe=0; for(u32 i=0;leaf[i];i++) if(leaf[i]=='|') pipe=leaf+i;
    if(pipe) { u32 i=0; while(pipe[1+i] && i+1<sizeof leaf){leaf[i]=pipe[1+i];i++;} leaf[i]=0; }
    if(same_name(leaf,"..")) return 0;
    if(!leaf[0] || same_name(leaf,".")) {
        u32 i=0; while(parent_path[i] && i+1<sizeof d->path){d->path[i]=parent_path[i];i++;}
        if(parent_path[i]) return 0; d->path[i]=0;
    } else {
        for(u32 i=0;leaf[i];i++)
            if(leaf[i]=='/'||leaf[i]=='\\'||leaf[i]==':'||leaf[i]=='['||leaf[i]==']') return 0;
        if(!msi_make_path(d->path,sizeof d->path,parent_path,leaf)) return 0;
    }
    d->state=2;
    return 1;
}
static int msi_load_directories(void) {
    MsiTable *t=msi_find_table("Directory");
    if(!t || !t->rows || t->rows>MSI_MAX_DIRS) return 0;
    msi_dir_count=t->rows;
    for(u32 i=0;i<msi_dir_count;i++) {
        MsiDirPath *d=&msi_dir_paths[i];
        d->id[0]=d->parent[0]=d->defdir[0]=d->path[0]=0; d->state=0;
        if(!msi_cell_text("Directory",i,0,d->id,sizeof d->id) ||
           !msi_cell_text("Directory",i,1,d->parent,sizeof d->parent) ||
           !msi_cell_text("Directory",i,2,d->defdir,sizeof d->defdir)) return 0;
    }
    for(u32 i=0;i<msi_dir_count;i++) if(!msi_resolve_dir(i,0)) return 0;
    return 1;
}
static int msi_preflight_dir_path(const char *path) {
    char work[512]; u32 n=0;
    while(path[n] && n+1<sizeof work){work[n]=path[n];n++;}
    if(path[n] || n<3 || work[0]!='C' || work[1]!=':' || work[2]!='\\') return 0;
    work[n]=0;
    for(u32 i=3;i<=n;i++) if(work[i]=='\\' || work[i]=='/' || !work[i]) {
        char saved=work[i]; work[i]=0;
        if(i>3) { u64 t=msi_path_type(work); if(t==1) return 0; }
        work[i]=saved;
    }
    return 1;
}
static int msi_ensure_directory(const char *path) {
    char work[512]; u32 n=0;
    while(path[n] && n+1<sizeof work){work[n]=path[n];n++;}
    if(path[n] || n<3 || work[0]!='C' || work[1]!=':' || work[2]!='\\') return 0;
    work[n]=0;
    for(u32 i=3;i<=n;i++) if(work[i]=='\\' || work[i]=='/' || !work[i]) {
        char saved=work[i]; work[i]=0;
        if(i>3) {
            u64 type=msi_path_type(work);
            if(type==1) return 0;
            if(type==0) {
                if(sy3(SYS_CreateDirectory,(u64)work,0,0)) {
                    if(!msi_record_change(work,1,0)) {
                        (void)sy3(SYS_RemoveDirectory,(u64)work,0,0); return 0;
                    }
                } else if(msi_path_type(work)!=2) return 0;
            } else if(type!=2) return 0;
        }
        work[i]=saved;
    }
    return 1;
}
static int msi_long_filename(const char *raw,char *dst,u32 cap) {
    const char *s=raw; for(u32 i=0;raw[i];i++) if(raw[i]=='|'){s=raw+i+1;break;}
    u32 n=0; while(s[n] && n+1<cap){dst[n]=s[n];n++;}
    if(s[n] || !n) return 0; dst[n]=0;
    if((n==1 && dst[0]=='.') || (n==2 && dst[0]=='.'&&dst[1]=='.')) return 0;
    for(u32 i=0;i<n;i++) if(dst[i]=='/'||dst[i]=='\\'||dst[i]==':'||
        (u8)dst[i]<32||(u8)dst[i]>=127) return 0;
    return 1;
}
static int msi_file_name_matches(const char *raw,const char *name) {
    if(same_name(raw,name)) return 1;
    for(u32 i=0;raw[i];i++) if(raw[i]=='|') {
        char shortname[240]; if(i>=sizeof shortname) return 0;
        for(u32 j=0;j<i;j++) shortname[j]=raw[j];
        shortname[i]=0;
        return same_name(name,shortname) || same_name(name,raw+i+1);
    }
    return 0;
}
static int msi_parse_decimal(const char *s,u32 *value) {
    if(!s || !s[0]) return 0;
    u32 v=0;
    for(u32 i=0;s[i];i++) {
        if(s[i]<'0'||s[i]>'9') return 0;
        if(v>(0xFFFFFFFFu-(u32)(s[i]-'0'))/10u)return 0;
        v=v*10u+(u32)(s[i]-'0');
    }
    *value=v;return 1;
}
static int msi_component_selected(const char *component) {
    MsiTable *fc=msi_find_table("FeatureComponents");
    if(!fc || !fc->rows) return 1;
    MsiTable *features=msi_find_table("Feature");
    if(!features || !features->rows) return 0;
    MsiTable *conditions=msi_find_table("Condition");
    if(conditions && conditions->rows) return -1;
    char level_text[64]; u32 install_level=1;
    if(msi_property_value("INSTALLLEVEL",level_text,sizeof level_text) &&
       level_text[0] && !msi_parse_decimal(level_text,&install_level)) return -1;
    for(u32 i=0;i<fc->rows;i++) {
        char feature_id[64],comp_id[64];
        if(!msi_cell_text("FeatureComponents",i,0,feature_id,sizeof feature_id) ||
           !msi_cell_text("FeatureComponents",i,1,comp_id,sizeof comp_id)) return -1;
        if(!same_name(comp_id,component)) continue;
        for(u32 j=0;j<features->rows;j++) {
            char key[64]; long long feature_level=0;
            if(!msi_cell_text("Feature",j,0,key,sizeof key) ||
               !msi_table_cell("Feature",j,5,0,0,&feature_level,0)) return -1;
            if(same_name(key,feature_id) && feature_level>0 &&
               (u64)feature_level<=install_level) return 1;
        }
    }
    return 0;
}
static int msi_build_file_plan(void) {
    MsiTable *files=msi_find_table("File"), *components=msi_find_table("Component");
    MsiTable *fc=msi_find_table("FeatureComponents");
    if(!files || !files->rows || files->rows>MSI_MAX_FILE_PLANS ||
       !components || !components->rows || !msi_dir_count ||
       (fc && fc->rows && !msi_find_table("Feature"))) {
        out("Install blocked: unsupported file/component/feature table layout.\n"); return 0;
    }
    msi_file_plan_count=0;
    for(u32 i=0;i<files->rows;i++) {
        char id[64],comp[64],raw[240],dirid[64],condition[128];
        long long size=0,seq=0; int cond_text=0;
        if(!msi_cell_text("File",i,0,id,sizeof id) ||
           !msi_cell_text("File",i,1,comp,sizeof comp) ||
           !msi_cell_text("File",i,2,raw,sizeof raw) ||
           !msi_table_cell("File",i,3,0,0,&size,0) ||
           !msi_table_cell("File",i,6,0,0,&seq,0) || size<0 ||
           size>0x7FFFFFFFLL || seq<0) return 0;
        int selected=msi_component_selected(comp);
        if(selected<0) { out("Install blocked: unsupported feature condition table.\n"); return 0; }
        if(!selected) continue;
        int component_row=-1;
        for(u32 j=0;j<components->rows;j++) {
            char key[64];
            if(msi_cell_text("Component",j,0,key,sizeof key) && same_name(key,comp)) {
                component_row=(int)j; break;
            }
        }
        if(component_row<0 || !msi_cell_text("Component",(u32)component_row,2,dirid,sizeof dirid))
            return 0;
        condition[0]=0;
        if(msi_table_cell("Component",(u32)component_row,4,condition,sizeof condition,0,&cond_text) && cond_text && condition[0]) {
            int condition_result=msi_condition_eval(condition);
            if(condition_result<0) return 0;
            if(!condition_result) continue;
        }
        MsiFilePlan *p=&msi_file_plans[msi_file_plan_count];
        p->id[0]=p->raw_name[0]=p->name[0]=p->target[0]=0;
        u32 j=0; while(id[j] && j+1<sizeof p->id){p->id[j]=id[j];j++;} if(id[j])return 0;p->id[j]=0;
        j=0; while(raw[j] && j+1<sizeof p->raw_name){p->raw_name[j]=raw[j];j++;} if(raw[j])return 0;p->raw_name[j]=0;
        if(!msi_long_filename(raw,p->name,sizeof p->name)) return 0;
        int di=msi_dir_index(dirid);
        char dirpath[512];
        if(di>=0) {
            j=0; while(msi_dir_paths[di].path[j] && j+1<sizeof dirpath){dirpath[j]=msi_dir_paths[di].path[j];j++;}
            if(msi_dir_paths[di].path[j]) return 0; dirpath[j]=0;
        } else if(!msi_builtin_dir(dirid,dirpath,sizeof dirpath)) return 0;
        if(!msi_make_path(p->target,sizeof p->target,dirpath,p->name) ||
           !msi_preflight_dir_path(dirpath) || msi_path_type(p->target)!=0) return 0;
        for(u32 k=0;k<msi_file_plan_count;k++)
            if(same_name(msi_file_plans[k].target,p->target)) return 0;
        p->size=(u32)size; p->sequence=seq; msi_file_plan_count++;
    }
    if(!msi_file_plan_count) return 0;
    return 1;
}
static int msi_copy_staged_files(void) {
    for(u32 i=0;i<msi_file_plan_count;i++) {
        MsiFilePlan *p=&msi_file_plans[i];
        int si=-1;
        for(u32 j=0;j<msi_stage_file_count;j++)
            if(msi_file_name_matches(p->raw_name,msi_stage_files[j].name)) { si=(int)j; break; }
        if(si<0 || msi_stage_files[si].size!=p->size) {
            out("Install blocked: File table entry has no matching CAB payload: ");out(p->name);out("\n");
            return 0;
        }
        char source[256];
        if(!msi_make_path(source,sizeof source,cab_stage_path,msi_stage_files[si].name) ||
           msi_path_type(source)!=1) return 0;
        char dirpath[512]; u32 n=0;
        while(p->target[n] && n+1<sizeof dirpath){dirpath[n]=p->target[n];n++;}
        if(p->target[n]) return 0;
        while(n && dirpath[n-1]!='\\' && dirpath[n-1]!='/') n--;
        if(!n) return 0;
        if(n==3) {dirpath[3]=0;} else dirpath[n-1]=0;
        if(!msi_ensure_directory(dirpath)) return 0;
        if(msi_path_type(p->target)!=0) return 0;
        long long src=(long long)sy3(SYS_OpenFile,(u64)source,0,3);
        if(src<0) return 0;
        long long dst=(long long)sy3(SYS_OpenFile,(u64)p->target,0x40000000u,1);
        if(dst<0) { (void)sy3(SYS_CloseFile,(u64)src,0,0); return 0; }
        if(!msi_record_change(p->target,0,0)) {
            (void)sy3(SYS_CloseFile,(u64)dst,0,0);
            (void)sy3(SYS_CloseFile,(u64)src,0,0);
            (void)sy3(26,(u64)p->target,0,0); return 0;
        }
        u32 copied=0; int ok=1;
        while(copied<p->size) {
            u32 want=p->size-copied; if(want>sizeof msi_copy_buffer) want=sizeof msi_copy_buffer;
            u64 got=sy3(SYS_ReadFile,(u64)src,(u64)msi_copy_buffer,want);
            if((long long)got<=0 || got>want) {ok=0;break;}
            u64 wrote=sy3(SYS_WriteFileHandle,(u64)dst,(u64)msi_copy_buffer,got);
            if((long long)wrote<0 || wrote!=got) {ok=0;break;}
            copied+=(u32)got;
        }
        (void)sy3(SYS_CloseFile,(u64)dst,0,0);
        (void)sy3(SYS_CloseFile,(u64)src,0,0);
        if(!ok || copied!=p->size) return 0;
    }
    return 1;
}
static int msi_preflight_custom_actions(MsiSequenceRow *rows,u32 count) {
    int init=-1;
    for(u32 i=0;i<count;i++) if(rows[i].sequence!=0 &&
       same_name(rows[i].action,"InstallInitialize")) {init=(int)i;break;}
    if(init<0) return 0;
    for(u32 i=0;i<count;i++) {
        msi_custom_preexecuted[i]=0;
        if(rows[i].sequence==0 || msi_action_supported(rows[i].action)) continue;
        int kind=msi_custom_action_kind(rows[i].action);
        if(!kind) return 0;
        if((kind==35 || kind==51) && (int)i>init) {
            out("Install blocked: SetDirectory/SetProperty custom actions must precede InstallInitialize.\n");
            return 0;
        }
        int cond=msi_condition_eval(rows[i].condition);
        if(cond<0) return 0;
        if((int)i<init) {
            msi_custom_preexecuted[i]=1;
            if(!cond) continue;
            if(kind==35 || kind==51) {
                if(!msi_execute_custom_action(rows[i].action)) return 0;
            } else if(kind==19) {
                (void)msi_execute_custom_action(rows[i].action);
                return 0;
            }
        }
    }
    return 1;
}
static int msi_install_package(void) {
    MsiSequenceRow *rows=msi_install_rows; u8 *enabled=msi_install_enabled; u32 count=0;
    msi_runtime_prop_count=0; msi_dir_override_count=0;
    if(!msi_db_ok || !msi_load_sequence(rows,enabled,512,&count) ||
       !msi_load_directories() ||
       !msi_preflight_custom_actions(rows,count) ||
       !msi_build_file_plan()) {
        out("Install preflight failed; no destination files changed.\n"); return 0;
    }
    msi_change_count=0; msi_tx_active=1;
    int initialized=0, installed_files=0, finalized=0;
    for(u32 i=0;i<count;i++) {
        if(!enabled[i]) continue;
        int cond=msi_condition_eval(rows[i].condition);
        if(cond<0) goto fail;
        if(!cond) continue;
        const char *a=rows[i].action;
        if(!msi_action_supported(a)) {
            int kind=msi_custom_action_kind(a);
            if(msi_custom_preexecuted[i]) continue;
            if(kind==19) { (void)msi_execute_custom_action(a); goto fail; }
            goto fail;
        }
        if(same_name(a,"InstallInitialize")) { initialized=1; continue; }
        if(same_name(a,"InstallFiles")) {
            if(!initialized || installed_files || !msi_extract_embedded_cabinets() ||
               !msi_copy_staged_files()) goto fail;
            installed_files=1; continue;
        }
        if(same_name(a,"InstallFinalize")) {
            if(!initialized || !installed_files) goto fail;
            finalized=1; break;
        }
        /* Costing and ResolveSource are intentionally no-op in this subset. */
    }
    if(!finalized || !msi_commit_cleanup()) goto fail;
    out("MSI file installation completed. Unsupported registration actions were not run.\n");
    return 1;
fail:
    out("MSI installation failed; rolling back files and directories created by this run.\n");
    msi_rollback();
    return 0;
}

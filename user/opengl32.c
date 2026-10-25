/*
 * SentinelOS OpenGL 1.1 compatibility runtime.
 *
 * This is a bounded software implementation over SentinelOS's mapped
 * framebuffer. It intentionally reports only the subset implemented here;
 * it is not a claim of complete desktop OpenGL conformance.
 */
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef unsigned int GLbitfield;
typedef float GLfloat;
typedef float GLclampf;
typedef void GLvoid;
typedef struct { unsigned long long va; unsigned int w,h,pitch; } OGL_FB;
typedef struct { unsigned int x,y,w,h; } OGL_RECT;
enum {
 GL_FALSE=0, GL_TRUE=1, GL_COLOR_BUFFER_BIT=0x4000, GL_DEPTH_BUFFER_BIT=0x0100,
 GL_STENCIL_BUFFER_BIT=0x0400, GL_TRIANGLES=4, GL_TRIANGLE_STRIP=5,
 GL_TRIANGLE_FAN=6, GL_QUADS=7, GL_LINES=1, GL_LINE_STRIP=3, GL_POINTS=0,
 GL_FLOAT=0x1406, GL_UNSIGNED_BYTE=0x1401, GL_UNSIGNED_SHORT=0x1403,
 GL_VERTEX_ARRAY=0x8074, GL_COLOR_ARRAY=0x8076, GL_TEXTURE_COORD_ARRAY=0x8078,
 GL_TEXTURE_2D=0x0DE1, GL_BLEND=0x0BE2, GL_DEPTH_TEST=0x0B71,
 GL_SCISSOR_TEST=0x0C11, GL_STENCIL_TEST=0x0B90, GL_DITHER=0x0BD0,
 GL_SRC_COLOR=0x0300, GL_ONE_MINUS_SRC_COLOR=0x0301, GL_SRC_ALPHA=0x0302, GL_ONE_MINUS_SRC_ALPHA=0x0303, GL_DST_ALPHA=0x0304, GL_ONE_MINUS_DST_ALPHA=0x0305, GL_DST_COLOR=0x0306, GL_ONE_MINUS_DST_COLOR=0x0307, GL_SRC_ALPHA_SATURATE=0x0308, GL_ONE=1, GL_ZERO=0,
 GL_TEXTURE_MIN_FILTER=0x2801, GL_TEXTURE_MAG_FILTER=0x2800,
 GL_TEXTURE_WRAP_S=0x2802, GL_TEXTURE_WRAP_T=0x2803, GL_NEAREST=0x2600,
 GL_LINEAR=0x2601, GL_CLAMP=0x2900, GL_REPEAT=0x2901,
 GL_VENDOR=0x1F00, GL_RENDERER=0x1F01, GL_VERSION=0x1F02,
 GL_EXTENSIONS=0x1F03, GL_NO_ERROR=0, GL_INVALID_ENUM=0x0500,
 GL_INVALID_VALUE=0x0501, GL_INVALID_OPERATION=0x0502
};
static unsigned int ogl_error, ogl_clear_color, ogl_enabled, ogl_next_texture=1;
static unsigned int ogl_blend_factor(GLenum factor,unsigned int src,unsigned int dst,unsigned int channel){unsigned int sa=src>>24,da=dst>>24,sc=(src>>(channel*8u))&255u,dc=(dst>>(channel*8u))&255u;if(factor==GL_ZERO)return 0;if(factor==GL_ONE)return 255;if(factor==GL_SRC_ALPHA)return sa;if(factor==GL_ONE_MINUS_SRC_ALPHA)return 255u-sa;if(factor==GL_DST_ALPHA)return da;if(factor==GL_ONE_MINUS_DST_ALPHA)return 255u-da;if(factor==GL_SRC_COLOR)return sc;if(factor==GL_ONE_MINUS_SRC_COLOR)return 255u-sc;if(factor==GL_DST_COLOR)return dc;if(factor==GL_ONE_MINUS_DST_COLOR)return 255u-dc;if(factor==GL_SRC_ALPHA_SATURATE)return channel==3?255u:(sa<(255u-da)?sa:(255u-da));return 0;}
static GLenum ogl_blend_src=GL_SRC_ALPHA,ogl_blend_dst=GL_ONE_MINUS_SRC_ALPHA;
static unsigned int ogl_pack_alignment=4,ogl_unpack_alignment=4;
static unsigned int ogl_view_x,ogl_view_y,ogl_view_w,ogl_view_h;
static unsigned int ogl_sc_x,ogl_sc_y,ogl_sc_w,ogl_sc_h;
static int ogl_current;
static const GLfloat *ogl_vertices,*ogl_colors,*ogl_texcoords;
static GLint ogl_vertex_size=2,ogl_color_size=4,ogl_tex_size=2;
static GLsizei ogl_vertex_stride,ogl_color_stride,ogl_tex_stride;
static GLuint ogl_bound_texture;
static unsigned int ogl_current_color=0xffffffffu;
static struct { GLuint id; unsigned int used, width, height, min_filter, mag_filter, wrap_s, wrap_t; } ogl_textures[8];
static unsigned int ogl_texture_pixels[8][512u*512u];
static int ogl_tex_array_enabled,ogl_color_array_enabled;
static unsigned long long ogl_sys(unsigned long long n,unsigned long long a,unsigned long long b,unsigned long long c) {
 unsigned long long r;
 __asm__ volatile("mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
  : [r]"=r"(r):[n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c):"rax","rdi","rsi","rdx","memory","cc");
 return r;
}
static OGL_FB *ogl_fb(void) {
 static OGL_FB fb;
 if (!fb.va || !fb.w || !fb.h) {
  if (!ogl_sys(8,(unsigned long long)&fb,0,0) || !fb.va || !fb.w || !fb.h || fb.pitch<fb.w*4u) return 0;
  ogl_view_w=fb.w; ogl_view_h=fb.h; ogl_sc_w=fb.w; ogl_sc_h=fb.h;
 }
 return &fb;
}
static unsigned int ogl_stride(GLsizei stride,unsigned int size) { return stride?(unsigned int)stride:size*4u; }
static unsigned int ogl_color_array_stride(void) { return ogl_color_stride?(unsigned int)ogl_color_stride:(unsigned int)ogl_color_size; }
static void ogl_pixel(int x,int y,unsigned int color) {
 OGL_FB *fb=ogl_fb(); if(!fb||x<0||y<0||(unsigned int)x>=fb->w||(unsigned int)y>=fb->h)return;
 if((ogl_enabled&GL_SCISSOR_TEST)&&(x<(int)ogl_sc_x||y<(int)ogl_sc_y||x>=(int)(ogl_sc_x+ogl_sc_w)||y>=(int)(ogl_sc_y+ogl_sc_h)))return;
 ((unsigned int*)(unsigned long long)fb->va)[(unsigned long long)y*(fb->pitch/4u)+(unsigned int)x]=color;
}
static unsigned int ogl_vertex_color(GLsizei i) {
 if(!ogl_color_array_enabled||!ogl_colors)return ogl_current_color;
 const unsigned char *p=(const unsigned char*)ogl_colors+(unsigned long long)i*ogl_color_array_stride();
 unsigned int r=(unsigned int)p[0],g=(unsigned int)p[1],b=(unsigned int)p[2],a=ogl_color_size>3?p[3]:255u;
 return (a<<24)|(r<<16)|(g<<8)|b;
}
static unsigned int ogl_lerp_color(unsigned int a,unsigned int b,unsigned int c,float wa,float wb,float wc) {
 unsigned int out=0;
 for(unsigned int sh=0;sh<32;sh+=8){float q=(float)((a>>sh)&255)*wa+(float)((b>>sh)&255)*wb+(float)((c>>sh)&255)*wc;unsigned int v=(unsigned int)(q+0.5f);if(v>255)v=255;out|=v<<sh;}
 return out;
}
static int ogl_coord(GLsizei i,int axis) {
 if(!ogl_vertices)return 0;
 const GLfloat *p=(const GLfloat*)((const unsigned char*)ogl_vertices+(unsigned long long)i*ogl_stride(ogl_vertex_stride,(unsigned int)ogl_vertex_size));
 GLfloat q=p[axis];
 OGL_FB *fb=ogl_fb(); if(!fb)return 0;
 if(q>=-1.01f&&q<=1.01f) q=(q+1.0f)*(float)(axis?ogl_view_h:ogl_view_w)*0.5f;
 if(axis) q=(float)ogl_view_h-q; q+=(float)(axis?ogl_view_y:ogl_view_x);
 if(q>32767.0f)q=32767.0f;if(q< -32768.0f)q=-32768.0f;
 return (int)q;
}
static void ogl_line(int x0,int y0,int x1,int y1,unsigned int c) {
 int dx=x1>x0?x1-x0:x0-x1, sx=x0<x1?1:-1;
 int dy=y1>y0?y0-y1:y1-y0, sy=y0<y1?1:-1,err=dx+dy;
 for(;;){ogl_pixel(x0,y0,c);if(x0==x1&&y0==y1)break;int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
}
static int ogl_find_texture(GLuint id){for(int i=0;i<8;i++)if(ogl_textures[i].used&&ogl_textures[i].id==id)return i;return -1;}
static float ogl_get_texcoord(GLsizei i,int axis){if(!ogl_texcoords)return 0.0f;const GLfloat *p=(const GLfloat*)((const unsigned char*)ogl_texcoords+(unsigned long long)i*ogl_stride(ogl_tex_stride,(unsigned int)ogl_tex_size));return p[axis<ogl_tex_size?axis:0];}
static unsigned int ogl_sample_texture(float u,float v){
 int k=ogl_find_texture(ogl_bound_texture);if(k<0)return 0xffffffffu;
 unsigned int w=ogl_textures[k].width,h=ogl_textures[k].height;if(!w||!h)return 0xffffffffu;
 if(ogl_textures[k].wrap_s==GL_REPEAT){u=u-(float)(int)u;if(u<0)u+=1.0f;}else{if(u<0)u=0;if(u>1)u=1;}
 if(ogl_textures[k].wrap_t==GL_REPEAT){v=v-(float)(int)v;if(v<0)v+=1.0f;}else{if(v<0)v=0;if(v>1)v=1;}
 float fx=u*(float)(w-1),fy=(1.0f-v)*(float)(h-1);
 if(ogl_textures[k].mag_filter!=GL_LINEAR||w==1||h==1){
  unsigned int x=(unsigned int)(fx+0.5f),y=(unsigned int)(fy+0.5f);
  if(x>=w)x=w-1;if(y>=h)y=h-1;
  return ogl_texture_pixels[k][(unsigned long long)y*w+x];
 }
 unsigned int x0=(unsigned int)fx,y0=(unsigned int)fy,x1=x0+1<w?x0+1:x0,y1=y0+1<h?y0+1:y0;
 float ax=fx-(float)x0,ay=fy-(float)y0;
 unsigned int c00=ogl_texture_pixels[k][(unsigned long long)y0*w+x0],c10=ogl_texture_pixels[k][(unsigned long long)y0*w+x1];
 unsigned int c01=ogl_texture_pixels[k][(unsigned long long)y1*w+x0],c11=ogl_texture_pixels[k][(unsigned long long)y1*w+x1],out=0;
 for(unsigned int sh=0;sh<32;sh+=8){
  float a=(float)((c00>>sh)&255)*(1.0f-ax)+(float)((c10>>sh)&255)*ax;
  float b=(float)((c01>>sh)&255)*(1.0f-ax)+(float)((c11>>sh)&255)*ax;
  unsigned int q=(unsigned int)(a*(1.0f-ay)+b*ay+0.5f);if(q>255)q=255;out|=q<<sh;
 }
 return out;
}
static void ogl_triangle(GLsizei a,GLsizei b,GLsizei c) {
 int x0=ogl_coord(a,0),y0=ogl_coord(a,1),x1=ogl_coord(b,0),y1=ogl_coord(b,1),x2=ogl_coord(c,0),y2=ogl_coord(c,1);
 unsigned int color=ogl_vertex_color(a);
 int minx=x0<x1?x0:x1,maxx=x0>x1?x0:x1,miny=y0<y1?y0:y1,maxy=y0>y1?y0:y1;
 if(x2<minx)minx=x2;if(x2>maxx)maxx=x2;if(y2<miny)miny=y2;if(y2>maxy)maxy=y2;
 if(maxx-minx>4096||maxy-miny>4096)return;
 long long area=(long long)(x1-x0)*(y2-y0)-(long long)(y1-y0)*(x2-x0);if(!area)return;
 for(int y=miny;y<=maxy;y++)for(int x=minx;x<=maxx;x++){
  long long w0=(long long)(x1-x)*(y2-y)-(long long)(y1-y)*(x2-x);
  long long w1=(long long)(x2-x)*(y0-y)-(long long)(y2-y)*(x0-x);
  long long w2=(long long)(x0-x)*(y1-y)-(long long)(y0-y)*(x1-x);
  if((area>0&&w0>=0&&w1>=0&&w2>=0)||(area<0&&w0<=0&&w1<=0&&w2<=0)){
   float wa=(float)w0/(float)area,wb=(float)w1/(float)area,wc=(float)w2/(float)area;
   unsigned int out=ogl_color_array_enabled?ogl_lerp_color(ogl_vertex_color(a),ogl_vertex_color(b),ogl_vertex_color(c),wa,wb,wc):color;
   if((ogl_enabled&GL_TEXTURE_2D)&&ogl_tex_array_enabled&&ogl_texcoords&&ogl_find_texture(ogl_bound_texture)>=0){
    float u=ogl_get_texcoord(a,0)*wa+ogl_get_texcoord(b,0)*wb+ogl_get_texcoord(c,0)*wc;
    float v=ogl_get_texcoord(a,1)*wa+ogl_get_texcoord(b,1)*wb+ogl_get_texcoord(c,1)*wc;
    unsigned int t=ogl_sample_texture(u,v);
    unsigned int tr=(t>>16)&255,tg=(t>>8)&255,tb=t&255,ta=t>>24;
    unsigned int cr=(out>>16)&255,cg=(out>>8)&255,cb=out&255,ca=out>>24;
    out=((ta*ca/255)<<24)|((tr*cr/255)<<16)|((tg*cg/255)<<8)|(tb*cb/255);
   }
   if(ogl_enabled&GL_BLEND){OGL_FB *fb=ogl_fb();if(fb&&x>=0&&y>=0&&(unsigned int)x<fb->w&&(unsigned int)y<fb->h){unsigned int *dp=(unsigned int*)(unsigned long long)fb->va+(unsigned long long)y*(fb->pitch/4u)+(unsigned int)x;unsigned int dst=*dp,rr=0,gg=0,bb=0,aa=0;for(unsigned int ch=0;ch<4;ch++){unsigned int sc=(out>>(ch*8u))&255u,dc=(dst>>(ch*8u))&255u,sf=ogl_blend_factor(ogl_blend_src,out,dst,ch),df=ogl_blend_factor(ogl_blend_dst,out,dst,ch),v=(sc*sf+dc*df)/255u;if(ch==0)bb=v;else if(ch==1)gg=v;else if(ch==2)rr=v;else aa=v;}out=(aa<<24)|(rr<<16)|(gg<<8)|bb;}}
   ogl_pixel(x,y,out);
  }
 }
}
__attribute__((dllexport)) void glClearColor(GLclampf r,GLclampf g,GLclampf b,GLclampf a) {
 unsigned int R=(unsigned int)(r<0?0:r>1?255:r*255),G=(unsigned int)(g<0?0:g>1?255:g*255),B=(unsigned int)(b<0?0:b>1?255:b*255),A=(unsigned int)(a<0?0:a>1?255:a*255);
 ogl_clear_color=(A<<24)|(R<<16)|(G<<8)|B;
}
__attribute__((dllexport)) void glClear(GLbitfield mask) {
 if(!(mask&GL_COLOR_BUFFER_BIT))return;OGL_FB *fb=ogl_fb();if(!fb)return;
 unsigned int *p=(unsigned int*)(unsigned long long)fb->va;
 for(unsigned int y=0;y<fb->h;y++)for(unsigned int x=0;x<fb->w;x++)
  if(!(ogl_enabled&GL_SCISSOR_TEST)||(x>=ogl_sc_x&&y>=ogl_sc_y&&x<ogl_sc_x+ogl_sc_w&&y<ogl_sc_y+ogl_sc_h))p[(unsigned long long)y*(fb->pitch/4u)+x]=ogl_clear_color;
}
__attribute__((dllexport)) void glViewport(GLint x,GLint y,GLsizei w,GLsizei h){if(w<0||h<0){ogl_error=GL_INVALID_VALUE;return;}ogl_view_x=x<0?0:(unsigned int)x;ogl_view_y=y<0?0:(unsigned int)y;ogl_view_w=(unsigned int)w;ogl_view_h=(unsigned int)h;}
__attribute__((dllexport)) void glScissor(GLint x,GLint y,GLsizei w,GLsizei h){if(w<0||h<0){ogl_error=GL_INVALID_VALUE;return;}if(x<0){w+=x;x=0;if(w<0)w=0;}if(y<0){h+=y;y=0;if(h<0)h=0;}ogl_sc_x=(unsigned int)x;ogl_sc_y=(unsigned int)y;ogl_sc_w=(unsigned int)w;ogl_sc_h=(unsigned int)h;}
__attribute__((dllexport)) void glEnable(GLenum c){if(c==GL_BLEND||c==GL_DEPTH_TEST||c==GL_STENCIL_TEST||c==GL_DITHER||c==GL_TEXTURE_2D||c==GL_SCISSOR_TEST)ogl_enabled|=c;else ogl_error=GL_INVALID_ENUM;}
__attribute__((dllexport)) void glDisable(GLenum c){if(c==GL_BLEND||c==GL_DEPTH_TEST||c==GL_STENCIL_TEST||c==GL_DITHER||c==GL_TEXTURE_2D||c==GL_SCISSOR_TEST)ogl_enabled&=~c;else ogl_error=GL_INVALID_ENUM;}
__attribute__((dllexport)) GLboolean glIsEnabled(GLenum c){if(c!=GL_BLEND&&c!=GL_DEPTH_TEST&&c!=GL_STENCIL_TEST&&c!=GL_DITHER&&c!=GL_TEXTURE_2D&&c!=GL_SCISSOR_TEST){ogl_error=GL_INVALID_ENUM;return GL_FALSE;}return (GLboolean)((ogl_enabled&c)!=0);}
__attribute__((dllexport)) GLenum glGetError(void){unsigned int e=ogl_error;ogl_error=0;return e;}
__attribute__((dllexport)) const GLubyte *glGetString(GLenum n){static const GLubyte vendor[]="SentinelOS";static const GLubyte renderer[]="SentinelOS framebuffer software rasterizer";static const GLubyte version[]="1.1 SentinelOS subset";static const GLubyte ext[]="";if(n==GL_VENDOR)return vendor;if(n==GL_RENDERER)return renderer;if(n==GL_VERSION)return version;if(n==GL_EXTENSIONS)return ext;ogl_error=GL_INVALID_ENUM;return 0;}
__attribute__((dllexport)) void glDrawArrays(GLenum mode,GLint first,GLsizei count){
 if(first<0||first>1000000||count<0||count>100000||!ogl_vertices){ogl_error=GL_INVALID_VALUE;return;}
 if(mode==GL_TRIANGLES){for(int i=0;i+2<count;i+=3)ogl_triangle(first+i,first+i+1,first+i+2);}
 else if(mode==GL_TRIANGLE_STRIP){for(int i=0;i+2<count;i++)if(i&1)ogl_triangle(first+i+1,first+i,first+i+2);else ogl_triangle(first+i,first+i+1,first+i+2);}
 else if(mode==GL_TRIANGLE_FAN){for(int i=1;i+1<count;i++)ogl_triangle(first,first+i,first+i+1);}
 else if(mode==GL_LINES){for(int i=0;i+1<count;i+=2)ogl_line(ogl_coord(first+i,0),ogl_coord(first+i,1),ogl_coord(first+i+1,0),ogl_coord(first+i+1,1),ogl_vertex_color(first+i));}
 else if(mode==GL_LINE_STRIP){for(int i=0;i+1<count;i++)ogl_line(ogl_coord(first+i,0),ogl_coord(first+i,1),ogl_coord(first+i+1,0),ogl_coord(first+i+1,1),ogl_vertex_color(first+i));}
 else if(mode==GL_POINTS){for(int i=0;i<count;i++)ogl_pixel(ogl_coord(first+i,0),ogl_coord(first+i,1),ogl_vertex_color(first+i));}
 else if(mode==GL_QUADS){for(int i=0;i+3<count;i+=4){ogl_triangle(first+i,first+i+1,first+i+2);ogl_triangle(first+i,first+i+2,first+i+3);}}
 else ogl_error=GL_INVALID_ENUM;
}
static unsigned int ogl_index_at(const unsigned char *p,GLenum type,int i){return type==GL_UNSIGNED_SHORT?((const unsigned short*)p)[i]:p[i];}
__attribute__((dllexport)) void glDrawElements(GLenum mode,GLsizei count,GLenum type,const void *indices){
 if(!indices||count<0||count>100000){ogl_error=GL_INVALID_VALUE;return;}
 if(type!=GL_UNSIGNED_SHORT&&type!=GL_UNSIGNED_BYTE){ogl_error=GL_INVALID_ENUM;return;}
 const unsigned char *p=(const unsigned char*)indices;
 if(!ogl_vertices){ogl_error=GL_INVALID_OPERATION;return;}
 if(mode==GL_TRIANGLES){for(int i=0;i+2<count;i+=3)ogl_triangle((int)ogl_index_at(p,type,i),(int)ogl_index_at(p,type,i+1),(int)ogl_index_at(p,type,i+2));}
 else if(mode==GL_TRIANGLE_STRIP){for(int i=0;i+2<count;i++){int a=(int)ogl_index_at(p,type,i),b=(int)ogl_index_at(p,type,i+1),d=(int)ogl_index_at(p,type,i+2);if(i&1)ogl_triangle(b,a,d);else ogl_triangle(a,b,d);}}
 else if(mode==GL_TRIANGLE_FAN){int base=(int)ogl_index_at(p,type,0);for(int i=1;i+1<count;i++)ogl_triangle(base,(int)ogl_index_at(p,type,i),(int)ogl_index_at(p,type,i+1));}
 else if(mode==GL_QUADS){for(int i=0;i+3<count;i+=4){int a=(int)ogl_index_at(p,type,i),b=(int)ogl_index_at(p,type,i+1),d=(int)ogl_index_at(p,type,i+2),e=(int)ogl_index_at(p,type,i+3);ogl_triangle(a,b,d);ogl_triangle(a,d,e);}}
 else if(mode==GL_LINES){for(int i=0;i+1<count;i+=2){int a=(int)ogl_index_at(p,type,i),b=(int)ogl_index_at(p,type,i+1);ogl_line(ogl_coord(a,0),ogl_coord(a,1),ogl_coord(b,0),ogl_coord(b,1),ogl_vertex_color(a));}}
 else if(mode==GL_LINE_STRIP){for(int i=0;i+1<count;i++){int a=(int)ogl_index_at(p,type,i),b=(int)ogl_index_at(p,type,i+1);ogl_line(ogl_coord(a,0),ogl_coord(a,1),ogl_coord(b,0),ogl_coord(b,1),ogl_vertex_color(a));}}
 else if(mode==GL_POINTS){for(int i=0;i<count;i++){int a=(int)ogl_index_at(p,type,i);ogl_pixel(ogl_coord(a,0),ogl_coord(a,1),ogl_vertex_color(a));}}
 else ogl_error=GL_INVALID_ENUM;
}
__attribute__((dllexport)) void glVertexPointer(GLint size,GLenum type,GLsizei stride,const void *p){if(size<2||size>4||type!=GL_FLOAT||stride<0){ogl_error=GL_INVALID_VALUE;return;}ogl_vertex_size=size;ogl_vertex_stride=stride;ogl_vertices=(const GLfloat*)p;}
__attribute__((dllexport)) void glColorPointer(GLint size,GLenum type,GLsizei stride,const void *p){if(size<3||size>4||stride<0||type!=GL_UNSIGNED_BYTE){ogl_error=GL_INVALID_ENUM;return;}ogl_color_size=size;ogl_color_stride=stride;ogl_colors=(const GLfloat*)p;}
__attribute__((dllexport)) void glTexCoordPointer(GLint size,GLenum type,GLsizei stride,const void *p){if(size<1||size>4||type!=GL_FLOAT||stride<0){ogl_error=GL_INVALID_VALUE;return;}ogl_tex_size=size;ogl_tex_stride=stride;ogl_texcoords=(const GLfloat*)p;}
__attribute__((dllexport)) void glEnableClientState(GLenum a){if(a==GL_TEXTURE_COORD_ARRAY){ogl_tex_array_enabled=1;return;}if(a==GL_COLOR_ARRAY){ogl_color_array_enabled=1;return;}if(a==GL_VERTEX_ARRAY)return;ogl_error=GL_INVALID_ENUM;}
__attribute__((dllexport)) void glDisableClientState(GLenum a){if(a==GL_TEXTURE_COORD_ARRAY){ogl_tex_array_enabled=0;return;}if(a==GL_COLOR_ARRAY){ogl_color_array_enabled=0;return;}if(a!=GL_VERTEX_ARRAY)ogl_error=GL_INVALID_ENUM;}
__attribute__((dllexport)) void glLineWidth(GLfloat w){if(w<=0)ogl_error=GL_INVALID_VALUE;}
static int ogl_valid_blend_factor(GLenum f){return f==GL_ZERO||f==GL_ONE||f==GL_SRC_COLOR||f==GL_ONE_MINUS_SRC_COLOR||f==GL_SRC_ALPHA||f==GL_ONE_MINUS_SRC_ALPHA||f==GL_DST_ALPHA||f==GL_ONE_MINUS_DST_ALPHA||f==GL_DST_COLOR||f==GL_ONE_MINUS_DST_COLOR||f==GL_SRC_ALPHA_SATURATE;}
__attribute__((dllexport)) void glBlendFunc(GLenum s,GLenum d){if(!ogl_valid_blend_factor(s)||!ogl_valid_blend_factor(d)||s==GL_SRC_ALPHA_SATURATE&&d==GL_SRC_ALPHA_SATURATE){ogl_error=GL_INVALID_ENUM;return;}ogl_blend_src=s;ogl_blend_dst=d;}
__attribute__((dllexport)) void glPixelStorei(GLenum p,GLint v){if(v!=1&&v!=2&&v!=4&&v!=8){ogl_error=GL_INVALID_VALUE;return;}if(p==0x0D05)ogl_pack_alignment=(unsigned int)v;else if(p==0x0CF5)ogl_unpack_alignment=(unsigned int)v;else ogl_error=GL_INVALID_ENUM;}
__attribute__((dllexport)) void glClearStencil(GLint s){(void)s;ogl_error=GL_INVALID_OPERATION;}
__attribute__((dllexport)) void glClearDepth(double d){(void)d;ogl_error=GL_INVALID_OPERATION;}
__attribute__((dllexport)) void glDepthMask(GLboolean b){(void)b;ogl_error=GL_INVALID_OPERATION;}
__attribute__((dllexport)) void glDepthFunc(GLenum f){(void)f;ogl_error=GL_INVALID_OPERATION;}
__attribute__((dllexport)) void glStencilMask(GLuint m){(void)m;ogl_error=GL_INVALID_OPERATION;}
__attribute__((dllexport)) void glStencilFunc(GLenum f,GLint r,GLuint m){(void)f;(void)r;(void)m;ogl_error=GL_INVALID_OPERATION;}
__attribute__((dllexport)) void glStencilOp(GLenum a,GLenum b,GLenum c){(void)a;(void)b;(void)c;ogl_error=GL_INVALID_OPERATION;}
__attribute__((dllexport)) void glTexParameteri(GLenum t,GLenum p,GLint v){if(t!=GL_TEXTURE_2D){ogl_error=GL_INVALID_ENUM;return;}int k=-1;for(int i=0;i<8;i++)if(ogl_textures[i].used&&ogl_textures[i].id==ogl_bound_texture)k=i;if(k<0){ogl_error=GL_INVALID_OPERATION;return;}if(p==GL_TEXTURE_MIN_FILTER&&(v==GL_NEAREST||v==GL_LINEAR))ogl_textures[k].min_filter=(unsigned int)v;else if(p==GL_TEXTURE_MAG_FILTER&&(v==GL_NEAREST||v==GL_LINEAR))ogl_textures[k].mag_filter=(unsigned int)v;else if((p==GL_TEXTURE_WRAP_S||p==GL_TEXTURE_WRAP_T)&&(v==GL_REPEAT||v==GL_CLAMP)){if(p==GL_TEXTURE_WRAP_S)ogl_textures[k].wrap_s=(unsigned int)v;else ogl_textures[k].wrap_t=(unsigned int)v;}else ogl_error=GL_INVALID_ENUM;}
__attribute__((dllexport)) void glTexImage2D(GLenum t,GLint level,GLint internal,GLsizei w,GLsizei h,GLint border,GLenum format,GLenum type,const void *pixels){(void)internal;if(t!=GL_TEXTURE_2D){ogl_error=GL_INVALID_ENUM;return;}int k=-1;for(int j=0;j<8;j++)if(ogl_textures[j].used&&ogl_textures[j].id==ogl_bound_texture)k=j;if(k<0||level!=0||border!=0||w<=0||h<=0||w>512||h>512){ogl_error=GL_INVALID_VALUE;return;}if(type!=GL_UNSIGNED_BYTE||(format!=0x1908&&format!=0x1907)){ogl_error=GL_INVALID_ENUM;return;}ogl_textures[k].width=(unsigned int)w;ogl_textures[k].height=(unsigned int)h;unsigned int *dst=ogl_texture_pixels[k];const unsigned char *src=(const unsigned char*)pixels;unsigned int channels=format==0x1908?4u:3u,row=(unsigned int)w*channels,align=ogl_unpack_alignment,rowstride=(row+align-1u)&~(align-1u);for(int y=0;y<h;y++)for(int x=0;x<w;x++){unsigned long long i=(unsigned long long)y*(unsigned int)w+(unsigned int)x;unsigned int r=src?src[(unsigned long long)y*rowstride+(unsigned int)x*channels]:0,g=src?src[(unsigned long long)y*rowstride+(unsigned int)x*channels+1]:0,b=src?src[(unsigned long long)y*rowstride+(unsigned int)x*channels+2]:0,a=src&&channels==4?src[(unsigned long long)y*rowstride+(unsigned int)x*channels+3]:src?255:0;dst[i]=(a<<24)|(r<<16)|(g<<8)|b;}}
__attribute__((dllexport)) void glBindTexture(GLenum t,GLuint id){if(t!=GL_TEXTURE_2D){ogl_error=GL_INVALID_ENUM;return;}if(id){int found=0;for(int i=0;i<8;i++)if(ogl_textures[i].used&&ogl_textures[i].id==id)found=1;if(!found){ogl_error=GL_INVALID_OPERATION;return;}}ogl_bound_texture=id;}
__attribute__((dllexport)) void glGenTextures(GLsizei n,GLuint *out){if(n<0||n>8||!out){ogl_error=GL_INVALID_VALUE;return;}for(int i=0;i<n;i++){int slot=-1;for(int j=0;j<8;j++)if(!ogl_textures[j].used){slot=j;break;}if(slot<0){out[i]=0;ogl_error=GL_INVALID_OPERATION;continue;}unsigned int id=ogl_next_texture++;if(!id)id=ogl_next_texture++;ogl_textures[slot].id=id;ogl_textures[slot].used=1;ogl_textures[slot].width=ogl_textures[slot].height=0;ogl_textures[slot].min_filter=ogl_textures[slot].mag_filter=GL_LINEAR;ogl_textures[slot].wrap_s=ogl_textures[slot].wrap_t=GL_REPEAT;out[i]=id;}}
__attribute__((dllexport)) void glDeleteTextures(GLsizei n,const GLuint *ids){if(n<0||n>8||(!ids&&n)){ogl_error=GL_INVALID_VALUE;return;}for(int i=0;i<n;i++){int k=-1;for(int j=0;j<8;j++)if(ogl_textures[j].used&&ogl_textures[j].id==ids[i])k=j;if(k>=0){ogl_textures[k].used=0;ogl_textures[k].width=ogl_textures[k].height=0;}if(ogl_bound_texture==ids[i])ogl_bound_texture=0;}}
__attribute__((dllexport)) void glGetIntegerv(GLenum p,GLint *v){if(!v)return;OGL_FB *fb;if(p==0x0BA2){v[0]=(GLint)ogl_view_x;v[1]=(GLint)ogl_view_y;v[2]=(GLint)ogl_view_w;v[3]=(GLint)ogl_view_h;}else if(p==0x0C10){v[0]=(GLint)ogl_sc_x;v[1]=(GLint)ogl_sc_y;v[2]=(GLint)ogl_sc_w;v[3]=(GLint)ogl_sc_h;}else if(p==0x0D33)v[0]=512;else if(p==0x0D3A){fb=ogl_fb();v[0]=fb?(GLint)fb->w:0;v[1]=fb?(GLint)fb->h:0;}else if(p==0x8069)v[0]=(GLint)ogl_bound_texture;else if(p==0x0B21||p==0x0B22)v[0]=1;else{ogl_error=GL_INVALID_ENUM;}}
__attribute__((dllexport)) void glGetFloatv(GLenum p,GLfloat *v){if(!v)return;if(p==0x0B21){v[0]=1.0f;return;}if(p==0x0B22){v[0]=1.0f;v[1]=1.0f;return;}if(p==0x0B00){v[0]=(float)((ogl_current_color>>16)&255)/255.0f;v[1]=(float)((ogl_current_color>>8)&255)/255.0f;v[2]=(float)(ogl_current_color&255)/255.0f;v[3]=(float)(ogl_current_color>>24)/255.0f;return;}if(p==0x0C22){v[0]=(float)((ogl_clear_color>>16)&255)/255.0f;v[1]=(float)((ogl_clear_color>>8)&255)/255.0f;v[2]=(float)(ogl_clear_color&255)/255.0f;v[3]=(float)(ogl_clear_color>>24)/255.0f;return;}ogl_error=GL_INVALID_ENUM;}
__attribute__((dllexport)) void glGetBooleanv(GLenum p,GLboolean *v){if(!v)return;if(p==GL_BLEND||p==GL_DEPTH_TEST||p==GL_STENCIL_TEST||p==GL_DITHER||p==GL_TEXTURE_2D||p==GL_SCISSOR_TEST){*v=(GLboolean)((ogl_enabled&p)!=0);return;}ogl_error=GL_INVALID_ENUM;}
__attribute__((dllexport)) void glReadPixels(GLint x,GLint y,GLsizei w,GLsizei h,GLenum format,GLenum type,void *out){OGL_FB *fb=ogl_fb();if(!fb||!out||w<0||h<0||w>4096||h>4096||format!=0x1908||type!=GL_UNSIGNED_BYTE){ogl_error=GL_INVALID_VALUE;return;}unsigned char *d=(unsigned char*)out;unsigned int *src=(unsigned int*)(unsigned long long)fb->va,rowbytes=(unsigned int)w*4u,rowstride=(rowbytes+ogl_pack_alignment-1u)&~(ogl_pack_alignment-1u);for(int j=0;j<h;j++)for(int i=0;i<w;i++){int sx=x+i,sy=y+j;unsigned int c=0;if(sx>=0&&sy>=0&&(unsigned int)sx<fb->w&&(unsigned int)sy<fb->h)c=src[(unsigned long long)sy*(fb->pitch/4u)+(unsigned int)sx];unsigned long long o=(unsigned long long)j*rowstride+(unsigned int)i*4u;d[o]=(unsigned char)(c>>16);d[o+1]=(unsigned char)(c>>8);d[o+2]=(unsigned char)c;d[o+3]=(unsigned char)(c>>24);}}
__attribute__((dllexport)) void glGetDoublev(GLenum p,double *v){(void)p;if(v)*v=0.0;ogl_error=GL_INVALID_OPERATION;}
__attribute__((dllexport)) void glFinish(void){OGL_FB *fb=ogl_fb();if(fb){OGL_RECT r={0,0,fb->w,fb->h};ogl_sys(33,(unsigned long long)&r,0,0);}}
__attribute__((dllexport)) void glFlush(void){glFinish();}
static int ogl_streq(const char *a,const char *b){if(!a||!b)return 0;while(*a&&*a==*b){a++;b++;}return *a==*b;}
__attribute__((dllexport)) void *wglGetProcAddress(const char *name){
 if(ogl_streq(name,"glClear"))return (void*)glClear;
 if(ogl_streq(name,"glClearColor"))return (void*)glClearColor;
 if(ogl_streq(name,"glViewport"))return (void*)glViewport;
 if(ogl_streq(name,"glScissor"))return (void*)glScissor;
 if(ogl_streq(name,"glEnable"))return (void*)glEnable;
 if(ogl_streq(name,"glDisable"))return (void*)glDisable;
 if(ogl_streq(name,"glIsEnabled"))return (void*)glIsEnabled;
 if(ogl_streq(name,"glGetError"))return (void*)glGetError;
 if(ogl_streq(name,"glGetString"))return (void*)glGetString;
 if(ogl_streq(name,"glDrawArrays"))return (void*)glDrawArrays;
 if(ogl_streq(name,"glDrawElements"))return (void*)glDrawElements;
 if(ogl_streq(name,"glBindTexture"))return (void*)glBindTexture;
 if(ogl_streq(name,"glGenTextures"))return (void*)glGenTextures;
 if(ogl_streq(name,"glDeleteTextures"))return (void*)glDeleteTextures;
 if(ogl_streq(name,"glTexImage2D"))return (void*)glTexImage2D;
 if(ogl_streq(name,"glTexParameteri"))return (void*)glTexParameteri;
 if(ogl_streq(name,"glReadPixels"))return (void*)glReadPixels;
 if(ogl_streq(name,"glBlendFunc"))return (void*)glBlendFunc;
 if(ogl_streq(name,"glFinish"))return (void*)glFinish;
 if(ogl_streq(name,"glFlush"))return (void*)glFlush;
 return 0;
}
__attribute__((dllexport)) int wglMakeCurrent(void *dc,void *rc){(void)dc;ogl_current=rc!=0;return ogl_current;}
__attribute__((dllexport)) int wglDeleteContext(void *rc){(void)rc;ogl_current=0;return 1;}
__attribute__((dllexport)) void *wglCreateContext(void *dc){(void)dc;if(!ogl_fb())return 0;ogl_current=1;return (void*)1;}
__attribute__((dllexport)) int wglShareLists(void *a,void *b){return a&&b;}
__attribute__((dllexport)) int SwapBuffers(void *dc){(void)dc;glFinish();return ogl_fb()!=0;}
__attribute__((dllexport)) void glDrawBuffer(GLenum mode){(void)mode;}
__attribute__((dllexport)) void glReadBuffer(GLenum mode){(void)mode;}
__attribute__((dllexport)) void glGetTexParameteriv(GLenum t,GLenum p,GLint *v){(void)t;(void)p;if(v)*v=0;ogl_error=GL_INVALID_OPERATION;}
__attribute__((dllexport)) void glGetTexLevelParameteriv(GLenum t,GLint l,GLenum p,GLint *v){(void)t;(void)l;(void)p;if(v)*v=0;ogl_error=GL_INVALID_OPERATION;}
__attribute__((dllexport)) void glTexSubImage2D(GLenum t,GLint l,GLint x,GLint y,GLsizei w,GLsizei h,GLenum format,GLenum type,const void *pixels){if(t!=GL_TEXTURE_2D){ogl_error=GL_INVALID_ENUM;return;}int k=ogl_find_texture(ogl_bound_texture);if(k<0||l!=0||x<0||y<0||w<0||h<0||(unsigned int)x+(unsigned int)w>ogl_textures[k].width||(unsigned int)y+(unsigned int)h>ogl_textures[k].height){ogl_error=GL_INVALID_VALUE;return;}if(type!=GL_UNSIGNED_BYTE||(format!=0x1908&&format!=0x1907)){ogl_error=GL_INVALID_ENUM;return;}const unsigned char *src=(const unsigned char*)pixels;unsigned int channels=format==0x1908?4u:3u,rowbytes=(unsigned int)w*channels,align=ogl_unpack_alignment,rowstride=(rowbytes+align-1u)&~(align-1u);for(int row=0;row<h;row++)for(int col=0;col<w;col++){unsigned long long si=(unsigned long long)row*rowstride+(unsigned int)col*channels;unsigned int r=src?src[si]:0,g=src?src[si+1]:0,b=src?src[si+2]:0,a=src&&channels==4?src[si+3]:src?255:0;ogl_texture_pixels[k][((unsigned long long)(unsigned int)(y+row)*ogl_textures[k].width)+(unsigned int)(x+col)]=(a<<24)|(r<<16)|(g<<8)|b;}}
__attribute__((dllexport)) void glColor4f(GLfloat r,GLfloat g,GLfloat b,GLfloat a){unsigned int R=(unsigned int)(r<0?0:r>1?255:r*255),G=(unsigned int)(g<0?0:g>1?255:g*255),B=(unsigned int)(b<0?0:b>1?255:b*255),A=(unsigned int)(a<0?0:a>1?255:a*255);ogl_current_color=(A<<24)|(R<<16)|(G<<8)|B;}
__attribute__((dllexport)) void glColor4ub(unsigned char r,unsigned char g,unsigned char b,unsigned char a){ogl_current_color=((unsigned int)a<<24)|((unsigned int)r<<16)|((unsigned int)g<<8)|b;}
__attribute__((dllexport)) void glClearAccum(GLfloat r,GLfloat g,GLfloat b,GLfloat a){(void)r;(void)g;(void)b;(void)a;ogl_error=GL_INVALID_OPERATION;}

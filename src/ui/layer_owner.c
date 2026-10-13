/* UI-thread owner. All persistent writes precede the stock IPC send. */
#include <string.h>
#include "controller.h"
#include "native_files.h"
#include "layers.h"
#include "layer_owner.h"
#include "thunks.h"
#include "stock_addrs.h"
static nf_backend storage;
static ctl_platform io;
static int ready, stock_on, peq_on;
static char migration_text[CTL_FILE_LIMIT+1];
static int no_get(void *p) { (void)p; return 0; }
static void no_set(void *p,int n) { (void)p; (void)n; }
static void no_call(void *p) { (void)p; }
static int commit(int stock, int peq) {
    char b[XM5_LAYERS_SIZE]; xm5_layers_encode(b,stock,peq);
    if (io.tmp_open(io.ctx) || io.tmp_write(io.ctx,b,sizeof b) ||
        io.tmp_flush(io.ctx) || io.tmp_fsync(io.ctx) ||
        io.tmp_close(io.ctx) || io.tmp_rename(io.ctx)) {
        io.tmp_discard(io.ctx); return 0;
    }
    stock_on=stock; peq_on=peq; return 1;
}
static int bootstrap(void) {
    if (ready) return 1;
    nf_owner owner={0,no_get,no_set,no_call,no_call};
    if (nf_init(&storage,&io,NF_TARGET_IMPORT_DIR,NF_TARGET_ACTIVE_DIR,&owner)) return 0;
#ifdef U6_OWNER_TEST
    extern long u6_owner_test_sys(void*,long,long,long,long,long);
    storage.sys=u6_owner_test_sys; /* QEMU -L path-prefix isolation only */
#endif
    storage.active_name="layers.conf"; storage.tmp_name="layers.conf.tmp";
    char b[XM5_LAYERS_SIZE+1]; unsigned n=0;
    int r=io.read_active(io.ctx,b,sizeof b,&n);
    int stock=*(volatile unsigned char*)A_equalizer_flag ? 1:0, peq=0;
    if (r == CTL_IO_OK && xm5_layers_parse(b,n,&stock,&peq)) {
        stock_on=stock; peq_on=peq;
    } else if (r == CTL_IO_MISSING) {
        /* PEQ04 used EQFLAG for PEQ. Detect only its exact persistent header.
         * A corrupt preset still identifies migration; the DSP/parser fails off. */
        storage.active_name=NF_ACTIVE_NAME;
        unsigned len=0;
        int legacy=io.read_active(io.ctx,migration_text,sizeof migration_text,&len);
        storage.active_name="layers.conf";
        if ((legacy==CTL_IO_OK || legacy==CTL_IO_TOO_LARGE) &&
            len>=sizeof(CTL_HEADER_PREFIX)-1 &&
            !strncmp(migration_text,CTL_HEADER_PREFIX,sizeof(CTL_HEADER_PREFIX)-1)) {
            peq=stock; stock=0;
        }
        if (!commit(stock,peq)) return 0;
    } else {
        /* Corrupt state is not reinterpreted as an old install. Fail off. */
        if (!commit(0,0)) return 0;
        stock=peq=0;
    }
    /* State file is migration/restart authority; writing it first makes a crash
     * between migration and config.ini update repeatable without double EQ. */
    *(volatile unsigned char*)A_equalizer_flag=(unsigned char)stock;
    s_write_int_config(stock,"PLAYSET","EQFLAG");
    ready=1; return 1;
}
int u6_peq_enabled(void) { return bootstrap() ? peq_on : 0; }
int u6_peq_set(int on) {
    if (!bootstrap()) return 0;
    if (on==peq_on) return 1;
    if (!commit(stock_on,on ? 1:0)) return 0;
    u6_send_equalizer(*(volatile int*)A_equalizer_mode); return 1;
}
void u6_send_equalizer(int mode) {
    if (!bootstrap()) return; /* never arm against an uncommitted state */
    int live=*(volatile unsigned char*)A_equalizer_flag ? 1:0;
    if (live!=stock_on && !commit(live,peq_on)) {
        *(volatile unsigned char*)A_equalizer_flag=(unsigned char)stock_on;
        s_write_int_config(stock_on,"PLAYSET","EQFLAG"); return;
    }
    float gains[10]={0};
    if (mode>=1 && mode<=11) {
        volatile float *values=(volatile float*)0x80d568u;
        for (int i=0;i<10;++i) gains[i]=values[mode*10+i]-6.0f;
    }
    int state=(stock_on || peq_on) ? (s_mclGetPlayStatus()==1 ? 2:1):0;
    s_mclSetEq(gains[0],gains[1],gains[2],gains[3],gains[4],gains[5],
               gains[6],gains[7],gains[8],gains[9],state);
}

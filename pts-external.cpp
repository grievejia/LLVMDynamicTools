#include "pts-external.h"
#include "variable.h"
#include "varInit.h"

#include <sstream>

using namespace llvm;

ExternalInfo extInfo;

struct InfoPair {
  const char *name;
  ExtFunctionType type;
};

ExternalInfo::ExternalInfo()
{
	// Each (name, type) pair will be inserted into the map
	// This list is a copy from Ben's SFS codes
	static const InfoPair infoPairs[] = {
	  //The current llvm-gcc puts in the \01.
	  {"\01creat64", EFT_NOOP},
	  {"\01fseeko64", EFT_NOOP},
	  {"\01fstat64", EFT_NOOP},
	  {"\01fstatvfs64", EFT_NOOP},
	  {"\01ftello64", EFT_NOOP},
	  {"\01getrlimit64", EFT_NOOP},
	  {"\01lstat64", EFT_NOOP},
	  {"\01open64", EFT_NOOP},
	  {"\01stat64", EFT_NOOP},
	  {"\01statvfs64", EFT_NOOP},
	  {"Gpm_GetEvent", EFT_NOOP},
	  {"Gpm_Open", EFT_NOOP},
	  {"RAND_seed", EFT_NOOP},
	  {"SSL_CTX_free", EFT_NOOP},
	  {"SSL_CTX_set_default_verify_paths", EFT_NOOP},
	  {"SSL_free", EFT_NOOP},
	  {"SSL_get_error", EFT_NOOP},
	  {"SSL_get_fd", EFT_NOOP},
	  {"SSL_pending", EFT_NOOP},
	  {"SSL_read", EFT_NOOP},
	  {"SSL_set_bio", EFT_NOOP},
	  {"SSL_set_connect_state", EFT_NOOP},
	  {"SSL_shutdown", EFT_NOOP},
	  {"SSL_state", EFT_NOOP},
	  {"SSL_write", EFT_NOOP},
	  {"Void_FreeCore", EFT_NOOP},
	  {"X509_STORE_CTX_get_error", EFT_NOOP},
	  {"XAllocColor", EFT_NOOP},
	  {"XCloseDisplay", EFT_NOOP},
	  {"XCopyArea", EFT_NOOP},
	  {"XCreateColormap", EFT_NOOP},
	  {"XCreatePixmap", EFT_NOOP},
	  {"XCreateWindow", EFT_NOOP},
	  {"XDrawPoint", EFT_NOOP},
	  {"XDrawString", EFT_NOOP},
	  {"XDrawText", EFT_NOOP},
	  {"XFillRectangle", EFT_NOOP},
	  {"XFillRectangles", EFT_NOOP},
	  {"XFree", EFT_NOOP},
	  {"XFreeColormap", EFT_NOOP},
	  {"XFreeColors", EFT_NOOP},
	  {"XFreeFont", EFT_NOOP},
	  {"XFreeFontNames", EFT_NOOP},
	  {"XFreeGC", EFT_NOOP},
	  {"XFreePixmap", EFT_NOOP},
	  {"XGetGCValues", EFT_NOOP},
	  {"XGetGeometry", EFT_NOOP},
	  {"XInternAtom", EFT_NOOP},
	  {"XMapWindow", EFT_NOOP},
	  {"XNextEvent", EFT_NOOP},
	  {"XPutImage", EFT_NOOP},
	  {"XQueryColor", EFT_NOOP},
	  {"XResizeWindow", EFT_NOOP},
	  {"XSelectInput", EFT_NOOP},
	  {"XSendEvent", EFT_NOOP},
	  {"XSetBackground", EFT_NOOP},
	  {"XSetClipMask", EFT_NOOP},
	  {"XSetClipOrigin", EFT_NOOP},
	  {"XSetFillStyle", EFT_NOOP},
	  {"XSetFont", EFT_NOOP},
	  {"XSetForeground", EFT_NOOP},
	  {"XSetFunction", EFT_NOOP},
	  {"XSetGraphicsExposures", EFT_NOOP},
	  {"XSetLineAttributes", EFT_NOOP},
	  {"XSetTile", EFT_NOOP},
	  {"XSetWMHints", EFT_NOOP},
	  {"XSetWMNormalHints", EFT_NOOP},
	  {"XSetWindowBackgroundPixmap", EFT_NOOP},
	  {"XStoreName", EFT_NOOP},
	  {"XSync", EFT_NOOP},
	  {"XVisualIDFromVisual", EFT_NOOP},
	  {"XWMGeometry", EFT_NOOP},
	  {"XtAppSetFallbackResources", EFT_NOOP},
	  {"XtCloseDisplay", EFT_NOOP},
	  {"XtDestroyApplicationContext", EFT_NOOP},
	  {"XtDestroyWidget", EFT_NOOP},
	  {"_IO_getc", EFT_NOOP},
	  {"_IO_putc", EFT_NOOP},
	  {"__assert_fail", EFT_NOOP},
	  {"__dn_expand", EFT_NOOP},
	  {"__dn_skipname", EFT_NOOP},
	  {"__res_nclose", EFT_NOOP},
	  {"__res_ninit", EFT_NOOP},
	  {"__res_nmkquery", EFT_NOOP},
	  {"__res_nsend", EFT_NOOP},
	  {"__res_query", EFT_NOOP},
	  {"__res_querydomain", EFT_NOOP},
	  {"__res_search", EFT_NOOP},
	  {"__sigsetjmp", EFT_NOOP},
	  {"_obstack_begin", EFT_NOOP},
	  {"_obstack_memory_used", EFT_NOOP},
	  {"_obstack_newchunk", EFT_NOOP},
	  {"_setjmp", EFT_NOOP},
	  {"accept", EFT_NOOP},
	  {"access", EFT_NOOP},
	  {"asprintf", EFT_NOOP},
	  {"atexit", EFT_NOOP},
	  {"atof", EFT_NOOP},
	  {"atoi", EFT_NOOP},
	  {"atol", EFT_NOOP},
	  {"bind", EFT_NOOP},
	  {"cfgetospeed", EFT_NOOP},
	  {"cfree", EFT_NOOP},
	  {"cfsetispeed", EFT_NOOP},
	  {"cfsetospeed", EFT_NOOP},
	  {"chdir", EFT_NOOP},
	  {"chmod", EFT_NOOP},
	  {"chown", EFT_NOOP},
	  {"chroot", EFT_NOOP},
	  {"clearerr", EFT_NOOP},
	  {"clearok", EFT_NOOP},
	  {"closedir", EFT_NOOP},
	  {"compress2", EFT_NOOP},
	  {"confstr", EFT_NOOP},
	  {"connect", EFT_NOOP},
	  {"crc32", EFT_NOOP},
	  {"creat", EFT_NOOP},
	  {"creat64", EFT_NOOP},
	  {"deflate", EFT_NOOP},
	  {"deflateEnd", EFT_NOOP},
	  {"deflateInit2_", EFT_NOOP},
	  {"deflateReset", EFT_NOOP},
	  {"delwin", EFT_NOOP},
	  {"dladdr", EFT_NOOP},
	  {"dlclose", EFT_NOOP},
	  {"execl", EFT_NOOP},
	  {"execle", EFT_NOOP},
	  {"execlp", EFT_NOOP},
	  {"execv", EFT_NOOP},
	  {"execve", EFT_NOOP},
	  {"execvp", EFT_NOOP},
	  {"fclose", EFT_NOOP},
	  {"feof", EFT_NOOP},
	  {"ferror", EFT_NOOP},
	  {"fflush", EFT_NOOP},
	  {"fgetc", EFT_NOOP},
	  {"fgetpos", EFT_NOOP},
	  {"fileno", EFT_NOOP},
	  {"flockfile", EFT_NOOP},
	  {"fnmatch", EFT_NOOP},
	  {"forkpty", EFT_NOOP},
	  {"fprintf", EFT_NOOP},
	  {"fputc", EFT_NOOP},
	  {"fputs", EFT_NOOP},
	  {"fread", EFT_NOOP},
	  {"free", EFT_NOOP},
	  {"free_all_mem", EFT_NOOP},
	  {"freeaddrinfo", EFT_NOOP},
	  {"frexp", EFT_NOOP},
	  {"fscanf", EFT_NOOP},
	  {"fseek", EFT_NOOP},
	  {"fseeko", EFT_NOOP},
	  {"fseeko64", EFT_NOOP},
	  {"fsetpos", EFT_NOOP},
	  {"fstat", EFT_NOOP},
	  {"fstat64", EFT_NOOP},
	  {"fstatfs", EFT_NOOP},
	  {"fstatvfs64", EFT_NOOP},
	  {"ftell", EFT_NOOP},
	  {"ftello", EFT_NOOP},
	  {"ftello64", EFT_NOOP},
	  {"ftok", EFT_NOOP},
	  {"funlockfile", EFT_NOOP},
	  {"fwrite", EFT_NOOP},
	  {"g_scanner_destroy", EFT_NOOP},
	  {"g_scanner_eof", EFT_NOOP},
	  {"g_scanner_get_next_token", EFT_NOOP},
	  {"g_scanner_input_file", EFT_NOOP},
	  {"g_scanner_scope_add_symbol", EFT_NOOP},
	  {"gcry_cipher_close", EFT_NOOP},
	  {"gcry_cipher_ctl", EFT_NOOP},
	  {"gcry_cipher_decrypt", EFT_NOOP},
	  {"gcry_cipher_map_name", EFT_NOOP},
	  {"gcry_cipher_open", EFT_NOOP},
	  {"gcry_md_close", EFT_NOOP},
	  {"gcry_md_ctl", EFT_NOOP},
	  {"gcry_md_get_algo", EFT_NOOP},
	  {"gcry_md_hash_buffer", EFT_NOOP},
	  {"gcry_md_map_name", EFT_NOOP},
	  {"gcry_md_open", EFT_NOOP},
	  {"gcry_md_setkey", EFT_NOOP},
	  {"gcry_md_write", EFT_NOOP},
	  {"gcry_mpi_add", EFT_NOOP},
	  {"gcry_mpi_add_ui", EFT_NOOP},
	  {"gcry_mpi_clear_highbit", EFT_NOOP},
	  {"gcry_mpi_print", EFT_NOOP},
	  {"gcry_mpi_release", EFT_NOOP},
	  {"gcry_sexp_release", EFT_NOOP},
	  {"getaddrinfo", EFT_NOOP},
	  {"getc_unlocked", EFT_NOOP},
	  {"getgroups", EFT_NOOP},
	  {"gethostname", EFT_NOOP},
	  {"getloadavg", EFT_NOOP},
	  {"getopt", EFT_NOOP},
	  {"getopt_long", EFT_NOOP},
	  {"getopt_long_only", EFT_NOOP},
	  {"getpeername", EFT_NOOP},
	  {"getresgid", EFT_NOOP},
	  {"getresuid", EFT_NOOP},
	  {"getrlimit", EFT_NOOP},
	  {"getrlimit64", EFT_NOOP},
	  {"getrusage", EFT_NOOP},
	  {"getsockname", EFT_NOOP},
	  {"getsockopt", EFT_NOOP},
	  {"gettimeofday", EFT_NOOP},
	  {"globfree", EFT_NOOP},
	  {"gnutls_pkcs12_bag_decrypt", EFT_NOOP},
	  {"gnutls_pkcs12_bag_deinit", EFT_NOOP},
	  {"gnutls_pkcs12_bag_get_count", EFT_NOOP},
	  {"gnutls_pkcs12_bag_get_type", EFT_NOOP},
	  {"gnutls_x509_crt_deinit", EFT_NOOP},
	  {"gnutls_x509_crt_get_dn_by_oid", EFT_NOOP},
	  {"gnutls_x509_crt_get_key_id", EFT_NOOP},
	  {"gnutls_x509_privkey_deinit", EFT_NOOP},
	  {"gnutls_x509_privkey_get_key_id", EFT_NOOP},
	  {"gzclose", EFT_NOOP},
	  {"gzeof", EFT_NOOP},
	  {"gzgetc", EFT_NOOP},
	  {"gzread", EFT_NOOP},
	  {"gzseek", EFT_NOOP},
	  {"gztell", EFT_NOOP},
	  {"gzwrite", EFT_NOOP},
	  {"iconv_close", EFT_NOOP},
	  {"inet_addr", EFT_NOOP},
	  {"inet_aton", EFT_NOOP},
	  {"inet_pton", EFT_NOOP},
	  {"inflate", EFT_NOOP},
	  {"inflateEnd", EFT_NOOP},
	  {"inflateInit2_", EFT_NOOP},
	  {"inflateInit_", EFT_NOOP},
	  {"inflateReset", EFT_NOOP},
	  {"initgroups", EFT_NOOP},
	  {"jpeg_CreateCompress", EFT_NOOP},
	  {"jpeg_CreateDecompress", EFT_NOOP},
	  {"jpeg_destroy", EFT_NOOP},
	  {"jpeg_finish_compress", EFT_NOOP},
	  {"jpeg_finish_decompress", EFT_NOOP},
	  {"jpeg_read_header", EFT_NOOP},
	  {"jpeg_read_scanlines", EFT_NOOP},
	  {"jpeg_resync_to_restart", EFT_NOOP},
	  {"jpeg_set_colorspace", EFT_NOOP},
	  {"jpeg_set_defaults", EFT_NOOP},
	  {"jpeg_set_linear_quality", EFT_NOOP},
	  {"jpeg_set_quality", EFT_NOOP},
	  {"jpeg_start_compress", EFT_NOOP},
	  {"jpeg_start_decompress", EFT_NOOP},
	  {"jpeg_write_scanlines", EFT_NOOP},
	  {"keypad", EFT_NOOP},
	  {"lchown", EFT_NOOP},
	  {"link", EFT_NOOP},
	  {"llvm.memset.i32", EFT_NOOP},
	  {"llvm.memset.p0i8.i32", EFT_NOOP},
	  {"llvm.memset.i64", EFT_NOOP},
	  {"llvm.memset.p0i8.i64", EFT_NOOP},
	  {"llvm.stackrestore", EFT_NOOP},
	  {"llvm.va_copy", EFT_NOOP},
	  {"llvm.va_end", EFT_NOOP},
	  {"llvm.va_start", EFT_NOOP},
	  {"longjmp", EFT_NOOP},
	  {"lstat", EFT_NOOP},
	  {"lstat64", EFT_NOOP},
	  {"mblen", EFT_NOOP},
	  {"mbrlen", EFT_NOOP},
	  {"mbrtowc", EFT_NOOP},
	  {"mbtowc", EFT_NOOP},
	  {"memcmp", EFT_NOOP},
	  {"mkdir", EFT_NOOP},
	  {"mknod", EFT_NOOP},
	  {"mkfifo", EFT_NOOP},
	  {"mkstemp", EFT_NOOP},
	  {"mkstemp64", EFT_NOOP},
	  {"mktime", EFT_NOOP},
	  {"modf", EFT_NOOP},
	  {"mprotect", EFT_NOOP},
	  {"munmap", EFT_NOOP},
	  {"nanosleep", EFT_NOOP},
	  {"nhfree", EFT_NOOP},
	  {"nodelay", EFT_NOOP},
	  {"obstack_free", EFT_NOOP},
	  {"open", EFT_NOOP},
	  {"open64", EFT_NOOP},
	  {"openlog", EFT_NOOP},
	  {"openpty", EFT_NOOP},
	  {"pathconf", EFT_NOOP},
	  {"pclose", EFT_NOOP},
	  {"perror", EFT_NOOP},
	  {"pipe", EFT_NOOP},
	  {"png_destroy_write_struct", EFT_NOOP},
	  {"png_init_io", EFT_NOOP},
	  {"png_set_bKGD", EFT_NOOP},
	  {"png_set_invert_alpha", EFT_NOOP},
	  {"png_set_invert_mono", EFT_NOOP},
	  {"png_write_end", EFT_NOOP},
	  {"png_write_info", EFT_NOOP},
	  {"png_write_rows", EFT_NOOP},
	  {"poll", EFT_NOOP},
	  {"pread64", EFT_NOOP},
	  {"printf", EFT_NOOP},
	  {"pthread_attr_destroy", EFT_NOOP},
	  {"pthread_attr_init", EFT_NOOP},
	  {"pthread_attr_setscope", EFT_NOOP},
	  {"pthread_attr_setstacksize", EFT_NOOP},
	  {"pthread_create", EFT_NOOP},
	  {"pthread_mutex_destroy", EFT_NOOP},
	  {"pthread_mutex_init", EFT_NOOP},
	  {"pthread_mutex_lock", EFT_NOOP},
	  {"pthread_mutex_unlock", EFT_NOOP},
	  {"pthread_mutexattr_destroy", EFT_NOOP},
	  {"pthread_mutexattr_init", EFT_NOOP},
	  {"pthread_mutexattr_settype", EFT_NOOP},
	  {"ptsname", EFT_NOOP},
	  {"putenv", EFT_NOOP},
	  {"puts", EFT_NOOP},
	  {"qsort", EFT_NOOP},
	  {"re_compile_fastmap", EFT_NOOP},
	  {"re_exec", EFT_NOOP},
	  {"re_search", EFT_NOOP},
	  {"read", EFT_NOOP},
	  {"readlink", EFT_NOOP},
	  {"recv", EFT_NOOP},
	  {"recvfrom", EFT_NOOP},
	  {"regcomp", EFT_NOOP},
	  {"regerror", EFT_NOOP},
	  {"remove", EFT_NOOP},
	  {"rename", EFT_NOOP},
	  {"rewind", EFT_NOOP},
	  {"rewinddir", EFT_NOOP},
	  {"rmdir", EFT_NOOP},
	  {"rresvport", EFT_NOOP},
	  {"safe_cfree", EFT_NOOP},
	  {"safe_free", EFT_NOOP},
	  {"safefree", EFT_NOOP},
	  {"safexfree", EFT_NOOP},
	  {"scrollok", EFT_NOOP},
	  {"select", EFT_NOOP},
	  {"sem_destroy", EFT_NOOP},
	  {"sem_init", EFT_NOOP},
	  {"sem_post", EFT_NOOP},
	  {"sem_trywait", EFT_NOOP},
	  {"sem_wait", EFT_NOOP},
	  {"send", EFT_NOOP},
	  {"sendto", EFT_NOOP},
	  {"setbuf", EFT_NOOP},
	  {"setenv", EFT_NOOP},
	  {"setgroups", EFT_NOOP},
	  {"setitimer", EFT_NOOP},
	  {"setrlimit", EFT_NOOP},
	  {"setsockopt", EFT_NOOP},
	  {"setvbuf", EFT_NOOP},
	  {"sigaction", EFT_NOOP},
	  {"sigaddset", EFT_NOOP},
	  {"sigaltstack", EFT_NOOP},
	  {"sigdelset", EFT_NOOP},
	  {"sigemptyset", EFT_NOOP},
	  {"sigfillset", EFT_NOOP},
	  {"sigisemptyset", EFT_NOOP},
	  {"sigismember", EFT_NOOP},
	  {"siglongjmp", EFT_NOOP},
	  {"sigprocmask", EFT_NOOP},
	  {"sigsuspend", EFT_NOOP},
	  {"sm_free", EFT_NOOP},
	  {"snprintf", EFT_NOOP},
	  {"socketpair", EFT_NOOP},
	  {"sprintf", EFT_NOOP},
	  {"sscanf", EFT_NOOP},
	  {"stat", EFT_NOOP},
	  {"stat64", EFT_NOOP},
	  {"statfs", EFT_NOOP},
	  {"statvfs", EFT_NOOP},
	  {"statvfs64", EFT_NOOP},
	  {"strcasecmp", EFT_NOOP},
	  {"strcmp", EFT_NOOP},
	  {"strcoll", EFT_NOOP},
	  {"strcspn", EFT_NOOP},
	  {"strfmon", EFT_NOOP},
	  {"strftime", EFT_NOOP},
	  {"strlen", EFT_NOOP},
	  {"strncasecmp", EFT_NOOP},
	  {"strncmp", EFT_NOOP},
	  {"strspn", EFT_NOOP},
	  {"symlink", EFT_NOOP},
	  {"sysinfo", EFT_NOOP},
	  {"syslog", EFT_NOOP},
	  {"system", EFT_NOOP},
	  {"tcgetattr", EFT_NOOP},
	  {"tcsetattr", EFT_NOOP},
	  {"tgetent", EFT_NOOP},
	  {"tgetflag", EFT_NOOP},
	  {"tgetnum", EFT_NOOP},
	  {"time", EFT_NOOP},
	  {"timegm", EFT_NOOP},
	  {"times", EFT_NOOP},
	  {"tputs", EFT_NOOP},
	  {"truncate", EFT_NOOP},
	  {"uname", EFT_NOOP},
	  {"uncompress", EFT_NOOP},
	  {"ungetc", EFT_NOOP},
	  {"unlink", EFT_NOOP},
	  {"unsetenv", EFT_NOOP},
	  {"utime", EFT_NOOP},
	  {"utimes", EFT_NOOP},
	  {"vasprintf", EFT_NOOP},
	  {"vfprintf", EFT_NOOP},
	  {"vim_free", EFT_NOOP},
	  {"vprintf", EFT_NOOP},
	  {"vsnprintf", EFT_NOOP},
	  {"vsprintf", EFT_NOOP},
	  {"waddch", EFT_NOOP},
	  {"waddnstr", EFT_NOOP},
	  {"wait", EFT_NOOP},
	  {"wait3", EFT_NOOP},
	  {"wait4", EFT_NOOP},
	  {"waitpid", EFT_NOOP},
	  {"wattr_off", EFT_NOOP},
	  {"wattr_on", EFT_NOOP},
	  {"wborder", EFT_NOOP},
	  {"wclrtobot", EFT_NOOP},
	  {"wclrtoeol", EFT_NOOP},
	  {"wcrtomb", EFT_NOOP},
	  {"wctomb", EFT_NOOP},
	  {"wctype", EFT_NOOP},
	  {"werase", EFT_NOOP},
	  {"wgetch", EFT_NOOP},
	  {"wmove", EFT_NOOP},
	  {"wrefresh", EFT_NOOP},
	  {"write", EFT_NOOP},
	  {"wtouchln", EFT_NOOP},
	  {"xfree", EFT_NOOP},

	  {"\01fopen64", EFT_ALLOC},
	  {"\01readdir64", EFT_ALLOC},
	  {"\01tmpfile64", EFT_ALLOC},
	  {"BIO_new_socket", EFT_ALLOC},
	  {"FT_Get_Sfnt_Table", EFT_ALLOC},
	  {"FcFontList", EFT_ALLOC},
	  {"FcFontMatch", EFT_ALLOC},
	  {"FcFontRenderPrepare", EFT_ALLOC},
	  {"FcFontSetCreate", EFT_ALLOC},
	  {"FcFontSort", EFT_ALLOC},
	  {"FcInitLoadConfig", EFT_ALLOC},
	  {"FcObjectSetBuild", EFT_ALLOC},
	  {"FcObjectSetCreate", EFT_ALLOC},
	  {"FcPatternBuild", EFT_ALLOC},
	  {"FcPatternCreate", EFT_ALLOC},
	  {"FcPatternDuplicate", EFT_ALLOC},
	  {"SSL_CTX_new", EFT_ALLOC},
	  {"SSL_get_peer_certificate", EFT_ALLOC},
	  {"SSL_new", EFT_ALLOC},
	  {"SSLv23_client_method", EFT_ALLOC},
	  {"SyGetmem", EFT_ALLOC},
	  {"TLSv1_client_method", EFT_ALLOC},
	  {"Void_ExtendCore", EFT_ALLOC},
	  {"XAddExtension", EFT_ALLOC},
	  {"XAllocClassHint", EFT_ALLOC},
	  {"XAllocSizeHints", EFT_ALLOC},
	  {"XAllocStandardColormap", EFT_ALLOC},
	  {"XCreateFontSet", EFT_ALLOC},
	  {"XCreateImage", EFT_ALLOC},
	  {"XCreateGC", EFT_ALLOC},
	  //Returns the prev. defined handler.
	  {"XESetCloseDisplay", EFT_ALLOC},
	  {"XGetImage", EFT_ALLOC},
	  {"XGetModifierMapping", EFT_ALLOC},
	  {"XGetMotionEvents", EFT_ALLOC},
	  {"XGetVisualInfo", EFT_ALLOC},
	  {"XLoadQueryFont", EFT_ALLOC},
	  {"XListPixmapFormats", EFT_ALLOC},
	  {"XRenderFindFormat", EFT_ALLOC},
	  {"XRenderFindStandardFormat", EFT_ALLOC},
	  {"XRenderFindVisualFormat", EFT_ALLOC},
	  {"XOpenDisplay", EFT_ALLOC},
	  //These 2 return the previous handler.
	  {"XSetErrorHandler", EFT_ALLOC},
	  {"XSetIOErrorHandler", EFT_ALLOC},
	  {"XShapeGetRectangles", EFT_ALLOC},
	  {"XShmCreateImage", EFT_ALLOC},
	  //This returns the handler last passed to XSetAfterFunction().
	  {"XSynchronize", EFT_ALLOC},
	  {"XcursorImageCreate", EFT_ALLOC},
	  {"XcursorLibraryLoadImages", EFT_ALLOC},
	  {"XcursorShapeLoadImages", EFT_ALLOC},
	  {"XineramaQueryScreens", EFT_ALLOC},
	  {"XkbGetMap", EFT_ALLOC},
	  {"XtAppCreateShell", EFT_ALLOC},
	  {"XtCreateApplicationContext", EFT_ALLOC},
	  {"XtOpenDisplay", EFT_ALLOC},
	  {"alloc", EFT_ALLOC},
	  {"alloc_check", EFT_ALLOC},
	  {"alloc_clear", EFT_ALLOC},
	  {"art_svp_from_vpath", EFT_ALLOC},
	  {"art_svp_vpath_stroke", EFT_ALLOC},
	  {"art_svp_writer_rewind_new", EFT_ALLOC},
	  //FIXME: returns arg0->svp
	  {"art_svp_writer_rewind_reap", EFT_ALLOC},
	  {"art_vpath_dash", EFT_ALLOC},
	  {"cairo_create", EFT_ALLOC},
	  {"cairo_image_surface_create_for_data", EFT_ALLOC},
	  {"cairo_pattern_create_for_surface", EFT_ALLOC},
	  {"cairo_surface_create_similar", EFT_ALLOC},
	  {"calloc", EFT_ALLOC},
	  {"fopen", EFT_ALLOC},
	  {"fopen64", EFT_ALLOC},
	  {"fopencookie", EFT_ALLOC},
	  {"g_scanner_new", EFT_ALLOC},
	  {"gcry_sexp_nth_mpi", EFT_ALLOC},
	  {"gzdopen", EFT_ALLOC},
	  {"iconv_open", EFT_ALLOC},
	  {"jpeg_alloc_huff_table", EFT_ALLOC},
	  {"jpeg_alloc_quant_table", EFT_ALLOC},
	  {"lalloc", EFT_ALLOC},
	  {"lalloc_clear", EFT_ALLOC},
	  {"malloc", EFT_ALLOC},
	  {"nhalloc", EFT_ALLOC},
	  {"oballoc", EFT_ALLOC},
	  {"pango_cairo_font_map_create_context", EFT_ALLOC},
	  //This may also point *arg2 to a new string.
	  {"pcre_compile", EFT_ALLOC},
	  {"pcre_study", EFT_ALLOC},
	  {"permalloc", EFT_ALLOC},
	  {"png_create_info_struct", EFT_ALLOC},
	  {"png_create_write_struct", EFT_ALLOC},
	  {"popen", EFT_ALLOC},
	  {"pthread_getspecific", EFT_ALLOC},
	  {"readdir", EFT_ALLOC},
	  {"readdir64", EFT_ALLOC},
	  {"safe_calloc", EFT_ALLOC},
	  {"safe_malloc", EFT_ALLOC},
	  {"safecalloc", EFT_ALLOC},
	  {"safemalloc", EFT_ALLOC},
	  {"safexcalloc", EFT_ALLOC},
	  {"safexmalloc", EFT_ALLOC},
	  {"savealloc", EFT_ALLOC},
	  {"setmntent", EFT_ALLOC},
	  {"shmat", EFT_ALLOC},
	  //These 2 return the previous handler.
	  {"signal", EFT_ALLOC},
	  {"sigset", EFT_ALLOC},
	  {"tempnam", EFT_ALLOC},
	  {"tmpfile", EFT_ALLOC},
	  {"tmpfile64", EFT_ALLOC},
	  {"xalloc", EFT_ALLOC},
	  {"xcalloc", EFT_ALLOC},
	  {"xmalloc", EFT_ALLOC},

	  {"\01mmap64", EFT_NOSTRUCT_ALLOC},
	  //FIXME: this is like realloc but with arg1.
	  {"X509_NAME_oneline", EFT_NOSTRUCT_ALLOC},
	  {"X509_verify_cert_error_string", EFT_NOSTRUCT_ALLOC},
	  {"XBaseFontNameListOfFontSet", EFT_NOSTRUCT_ALLOC},
	  {"XGetAtomName", EFT_NOSTRUCT_ALLOC},
	  {"XGetDefault", EFT_NOSTRUCT_ALLOC},
	  {"XGetKeyboardMapping", EFT_NOSTRUCT_ALLOC},
	  {"XListDepths", EFT_NOSTRUCT_ALLOC},
	  {"XListFonts", EFT_NOSTRUCT_ALLOC},
	  {"XSetLocaleModifiers", EFT_NOSTRUCT_ALLOC},
	  {"XcursorGetTheme", EFT_NOSTRUCT_ALLOC},
	  {"__strdup", EFT_NOSTRUCT_ALLOC},
	  {"crypt", EFT_NOSTRUCT_ALLOC},
	  {"ctime", EFT_NOSTRUCT_ALLOC},
	  {"dlerror", EFT_NOSTRUCT_ALLOC},
	  {"dlopen", EFT_NOSTRUCT_ALLOC},
	  {"gai_strerror", EFT_NOSTRUCT_ALLOC},
	  {"gcry_cipher_algo_name", EFT_NOSTRUCT_ALLOC},
	  {"gcry_md_algo_name", EFT_NOSTRUCT_ALLOC},
	  {"gcry_md_read", EFT_NOSTRUCT_ALLOC},
	  {"getenv", EFT_NOSTRUCT_ALLOC},
	  {"getlogin", EFT_NOSTRUCT_ALLOC},
	  {"getpass", EFT_NOSTRUCT_ALLOC},
	  {"gnutls_strerror", EFT_NOSTRUCT_ALLOC},
	  {"gpg_strerror", EFT_NOSTRUCT_ALLOC},
	  {"gzerror", EFT_NOSTRUCT_ALLOC},
	  {"inet_ntoa", EFT_NOSTRUCT_ALLOC},
	  {"initscr", EFT_NOSTRUCT_ALLOC},
	  {"llvm.stacksave", EFT_NOSTRUCT_ALLOC},
	  {"mmap", EFT_NOSTRUCT_ALLOC},
	  {"mmap64", EFT_NOSTRUCT_ALLOC},
	  {"newwin", EFT_NOSTRUCT_ALLOC},
	  {"nl_langinfo", EFT_NOSTRUCT_ALLOC},
	  {"opendir", EFT_NOSTRUCT_ALLOC},
	  {"sbrk", EFT_NOSTRUCT_ALLOC},
	  {"strdup", EFT_NOSTRUCT_ALLOC},
	  {"strerror", EFT_NOSTRUCT_ALLOC},
	  {"strsignal", EFT_NOSTRUCT_ALLOC},
	  {"textdomain", EFT_NOSTRUCT_ALLOC},
	  {"tgetstr", EFT_NOSTRUCT_ALLOC},
	  {"tigetstr", EFT_NOSTRUCT_ALLOC},
	  {"tmpnam", EFT_NOSTRUCT_ALLOC},
	  {"ttyname", EFT_NOSTRUCT_ALLOC},

	  {"__ctype_b_loc", EFT_STAT2},
	  {"__ctype_tolower_loc", EFT_STAT2},
	  {"__ctype_toupper_loc", EFT_STAT2},

	  {"XKeysymToString", EFT_STAT},
	  {"__errno_location", EFT_STAT},
	  {"__h_errno_location", EFT_STAT},
	  {"__res_state", EFT_STAT},
	  {"asctime", EFT_STAT},
	  {"bindtextdomain", EFT_STAT},
	  {"bind_textdomain_codeset", EFT_STAT},
	  //This is L_A0 when arg0 is not null.
	  {"ctermid", EFT_STAT},
	  {"dcgettext", EFT_STAT},
	  {"dgettext", EFT_STAT},
	  {"dngettext", EFT_STAT},
	  {"fdopen", EFT_STAT},
	  {"gcry_strerror", EFT_STAT},
	  {"gcry_strsource", EFT_STAT},
	  {"getgrgid", EFT_STAT},
	  {"getgrnam", EFT_STAT},
	  {"gethostbyaddr", EFT_STAT},
	  {"gethostbyname", EFT_STAT},
	  {"gethostbyname2", EFT_STAT},
	  {"getmntent", EFT_STAT},
	  {"getprotobyname", EFT_STAT},
	  {"getprotobynumber", EFT_STAT},
	  {"getpwent", EFT_STAT},
	  {"getpwnam", EFT_STAT},
	  {"getpwuid", EFT_STAT},
	  {"getservbyname", EFT_STAT},
	  {"getservbyport", EFT_STAT},
	  {"getspnam", EFT_STAT},
	  {"gettext", EFT_STAT},
	  {"gmtime", EFT_STAT},
	  {"gnu_get_libc_version", EFT_STAT},
	  {"gnutls_check_version", EFT_STAT},
	  {"localeconv", EFT_STAT},
	  {"localtime", EFT_STAT},
	  {"ngettext", EFT_STAT},
	  {"pango_cairo_font_map_get_default", EFT_STAT},
	  {"re_comp", EFT_STAT},
	  {"setlocale", EFT_STAT},
	  {"tgoto", EFT_STAT},
	  {"tparm", EFT_STAT},
	  {"zError", EFT_STAT},

	  {"getcwd", EFT_REALLOC},
	  {"mem_realloc", EFT_REALLOC},
	  {"realloc", EFT_REALLOC},
	  {"realloc_obj", EFT_REALLOC},
	  {"safe_realloc", EFT_REALLOC},
	  {"saferealloc", EFT_REALLOC},
	  {"safexrealloc", EFT_REALLOC},
	  //FIXME: when arg0 is null, the return points into the string that was
	  //  last passed in arg0 (rather than a new string, as for realloc).
	  {"strtok", EFT_REALLOC},
	  //As above, but also stores the last string into *arg2.
	  {"strtok_r", EFT_REALLOC},
	  {"xrealloc", EFT_REALLOC},

	  {"__rawmemchr", EFT_L_A0},
	  {"cairo_surface_reference", EFT_L_A0},
	  {"dlsym", EFT_L_A0},
	  {"fgets", EFT_L_A0},
	  {"jpeg_std_error", EFT_L_A0},
	  {"memchr", EFT_L_A0},
	  //This will overwrite *arg0 with non-pointer data -
	  //  assume that no valid pointer values are created.
	  {"memset", EFT_L_A0},
	  //This may return a new ptr if the region was moved.
	  {"mremap", EFT_L_A0},
	  {"stpcpy", EFT_L_A0},
	  {"strcat", EFT_L_A0},
	  {"strchr", EFT_L_A0},
	  {"strcpy", EFT_L_A0},
	  {"strerror_r", EFT_L_A0},
	  {"strncat", EFT_L_A0},
	  {"strncpy", EFT_L_A0},
	  {"strpbrk", EFT_L_A0},
	  {"strptime", EFT_L_A0},
	  {"strrchr", EFT_L_A0},
	  {"strstr", EFT_L_A0},
	  {"tmpnam_r", EFT_L_A0},
	  {"asctime_r", EFT_L_A1},
	  {"bsearch", EFT_L_A1},
	  {"getmntent_r", EFT_L_A1},
	  {"gmtime_r", EFT_L_A1},
	  {"gzgets", EFT_L_A1},
	  {"localtime_r", EFT_L_A1},
	  {"realpath", EFT_L_A1},
	  {"\01freopen64", EFT_L_A2},
	  //FIXME: may do L_A3 if arg5 > 0.
	  {"_XGetAsyncReply", EFT_L_A2},
	  {"freopen", EFT_L_A2},
	  {"freopen64", EFT_L_A2},
	  {"gcvt", EFT_L_A2},
	  {"inet_ntop", EFT_L_A2},
	  {"XGetSubImage", EFT_L_A8},

	  {"llvm.memcpy.i32", EFT_L_A0__A0R_A1R},
	  {"llvm.memcpy.p0i8.p0i8.i32", EFT_L_A0__A0R_A1R},
	  {"llvm.memcpy.i64", EFT_L_A0__A0R_A1R},
	  {"llvm.memcpy.p0i8.p0i8.i64", EFT_L_A0__A0R_A1R},
	  {"llvm.memmove.i32", EFT_L_A0__A0R_A1R},
	  {"llvm.memmove.p0i8.p0i8.i32", EFT_L_A0__A0R_A1R},
	  {"memcpy", EFT_L_A0__A0R_A1R},
	  {"memccpy", EFT_L_A0__A0R_A1R},
	  {"memmove", EFT_L_A0__A0R_A1R},
	  {"bcopy", EFT_A1R_A0R},
	  {"iconv", EFT_A3R_A1R_NS},

	  {"strtod", EFT_A1R_A0},
	  {"strtof", EFT_A1R_A0},
	  {"strtol", EFT_A1R_A0},
	  {"strtold", EFT_A1R_A0},
	  {"strtoll", EFT_A1R_A0},
	  {"strtoul", EFT_A1R_A0},
	  {"readdir_r", EFT_A2R_A1},
	  //These also set arg1->pw_name etc. to new strings.
	  {"getpwnam_r", EFT_A4R_A1},
	  {"getpwuid_r", EFT_A4R_A1},

	  {"db_create", EFT_A0R_NEW},
	  {"gcry_mpi_scan", EFT_A0R_NEW},
	  {"gcry_pk_decrypt", EFT_A0R_NEW},
	  {"gcry_sexp_build", EFT_A0R_NEW},
	  {"gnutls_pkcs12_bag_init", EFT_A0R_NEW},
	  {"gnutls_pkcs12_init", EFT_A0R_NEW},
	  {"gnutls_x509_crt_init", EFT_A0R_NEW},
	  {"gnutls_x509_privkey_init", EFT_A0R_NEW},
	  {"posix_memalign", EFT_A0R_NEW},
	  {"scandir", EFT_A1R_NEW},
	  {"XGetRGBColormaps", EFT_A2R_NEW},
	  {"XmbTextPropertyToTextList", EFT_A2R_NEW},
	  {"XQueryTree", EFT_A4R_NEW},
	  {"XGetWindowProperty", EFT_A11R_NEW},

	  //This must be the last entry.
	  {0, EFT_NOOP}
	};
	
	for (const InfoPair* itr = infoPairs; itr->name != NULL; ++itr)
		info[itr->name]= itr->type;
}

bool ExternalInfo::isExternal(Function* f)
{
	return info.count(f->getName());
}

ExtFunctionType ExternalInfo::getType(Function* f)
{
	assert(info.count(f->getName()));
	return info[f->getName()];
}

inline void copyPtr(Variable* dst, Variable* src)
{
	ptsGraph.update(dst, ptsGraph.lookup(src));
}

inline void doStore(Variable* dst, Variable* src)
{
	PtsSet dstTgt = ptsGraph.lookup(dst);
	PtsSet srcTgt = ptsGraph.lookup(src);
	ptsGraph.update(dstTgt, srcTgt);
}

unsigned getMaxOffset(Value* v)
{
	
	// If v is a CE or bitcast, the actual pointer type is its operand
	if (ConstantExpr* exp = dyn_cast<ConstantExpr>(v))
		v = exp->getOperand(0);
	else if (BitCastInst* inst = dyn_cast<BitCastInst>(v))
		v = inst->getOperand(0);
	// For other values, use the biggest struct type out of all operands
	if (User* user = dyn_cast<User>(v))
	{
		unsigned maxSize = 1;                         //the max size seen so far
		for (User::op_iterator itr = user->op_begin(), ite = user->op_end(); itr != ite; ++itr)
		{
			Value* operand = itr->get();
			const PointerType* ptrType = dyn_cast<PointerType>(operand->getType());
			if(ptrType == NULL)
				continue;
			
			const Type* opType = ptrType->getElementType();
			while(const ArrayType* arrayType = dyn_cast<ArrayType>(opType))
				opType = arrayType->getElementType();
			
			if(const StructType* stType = dyn_cast<StructType>(opType))
			{
				assert(structInfoMap.count(stType) && "structInfoMap should have info for all structs!");
				StructInfo* stInfo = structInfoMap[stType];
				unsigned size = stInfo->getExpandedSize();
				if (maxSize < size)
					maxSize = size;
			}
		}
		return maxSize;
	}
	else		// v has no operand
		return 1;
}

void copyMem(Value* arg0, Value* arg1, Variable* dstVar, bool noStruct = false)
{
	Variable* arg0Var = ptrToVariable(arg0);
	Variable* arg1Var = ptrToVariable(arg1);
	if (arg0Var == NULL || arg1Var == NULL)
		return;
	
	unsigned maxOffset = 1;
	if (!noStruct)
		// This might be problematic. We really should look at the third argument of memcpy for copy size
		maxOffset = std::max(getMaxOffset(arg0), getMaxOffset(arg1));

	for (unsigned i = 0; i < maxOffset; ++i)
	{	
		Variable* fromVar = variableFactory.getVariable(arg1Var->getIndex() + i);
		Variable* toVar = variableFactory.getVariable(arg0Var->getIndex() + i);
		//errs() << "fromVar = " << *fromVar << ", toVar = " << *toVar << "\n";
		
		PtsSet fromTgt = ptsGraph.lookup(fromVar);
		PtsSet fromTgtTgt = ptsGraph.lookup(fromTgt);
		PtsSet toTgt = ptsGraph.lookup(toVar);
		ptsGraph.update(toTgt, fromTgtTgt);
	}
	
	if (dstVar != NULL)
		copyPtr(dstVar, arg0Var);
}

const Type* traceAllocType(Instruction* inst)
{
	assert(inst != NULL);
	const PointerType* pType = cast<PointerType>(inst->getType());
	const Type* elemType = pType->getElementType();
	
	const Type* ret = NULL;
	unsigned maxSize = 0;
	while (const ArrayType* arrayType = dyn_cast<ArrayType>(elemType))
		elemType = arrayType->getElementType();
	if (const StructType* stType = dyn_cast<StructType>(elemType))
	{
		assert(structInfoMap.count(stType) && "structInfoMap should have info for all structs!");
		StructInfo* stInfo = structInfoMap[stType];
		ret = stType;
		maxSize = stInfo->getExpandedSize();
	}
	
	// Check all the users of inst
	for (Value::use_iterator itr = inst->use_begin(), ite = inst->use_end(); itr != ite; ++itr)
	{
		CastInst* castInst = dyn_cast<CastInst>(*itr);
		if (castInst == NULL)
			continue;
		//Only check casts to other ptr types.
		const PointerType* castType = dyn_cast<PointerType>(castInst->getType());
		if (castType == NULL)
			continue;
		
		const Type* elemType = castType->getElementType();
		while (const ArrayType* arrayType = dyn_cast<ArrayType>(elemType))
			elemType = arrayType->getElementType();
			
		unsigned size = 1;
		if (const StructType* stType = dyn_cast<StructType>(elemType))
		{
			assert(structInfoMap.count(stType) && "structInfoMap should have info for all structs!");
			StructInfo* stInfo = structInfoMap[stType];
			size = stInfo->getExpandedSize();
		}
		if (size > maxSize)
		{
			ret = elemType;
			maxSize = size;
		}
	}
	
	// If the allocation is of non-struct type and we can't find any casts, assume that it may be cast to the largest struct later on.
	if (ret == NULL)
		return StructInfo::maxStruct;
	else
		return ret;
}

void allocHeapSingle(Variable* dstVar, Instruction* allocSite)
{
	assert(dstVar != NULL);
	
	AddrTakenVar* memObj = variableFactory.getNextAddrTakenVar(false, false);
	memObj->name = dstVar->name + "_tgt_malloc";
	variableFactory.setAllocSite(memObj, allocSite);
	ptsGraph.update(dstVar, memObj);
}

void allocHeap(Variable* dst, CallSite* cs, bool noStruct = false)
{
	assert(dstVar != NULL);
	
	Instruction* inst = cs->getInstruction();
	if (noStruct)
	{
		allocHeapSingle(dst, inst);
		return;
	}
	
	const Type* type = traceAllocType(inst);
	unsigned size = 1;
	StructInfo* stInfo = NULL;
	if (const StructType* stType = dyn_cast<StructType>(type))
	{
		assert(structInfoMap.count(stType) && "structInfoMap should have info for all structs!");
		stInfo = structInfoMap[stType];
		if (stInfo->isEmpty())
			return;
		size = stInfo->getExpandedSize();
	}
	
	if (size == 1)
	{
		allocHeapSingle(dst, inst);
		return;
	}
	else if (size == 0)
		return;
	
	assert(stInfo != NULL);
	// First create all top-level vars so that they have contiguous indicies
	std::vector<TopLevelVar*> fields;
	for (unsigned i = 0; i < size; ++i)
	{
		TopLevelVar* topVar = variableFactory.getNextTopLevelVar(false, stInfo->isFieldArray(i), size - i);
		std::stringstream ss;
		ss << i;
		topVar->name = dst->name + "+" + ss.str();
		fields.push_back(topVar);
	}
	
	// Next create all addr-taken vars
	for (unsigned i = 0; i < size; ++i)
	{
		AddrTakenVar* atVar = variableFactory.getNextAddrTakenVar(false, stInfo->isFieldArray(i), size - i);
		TopLevelVar* topVar = fields[i];
		ptsGraph.update(topVar, atVar);
		atVar->name = topVar->name + "_tgt_malloc";
		variableFactory.setAllocSite(atVar, inst);
	}
}

void ptsAnalyzeExtFunction(CallSite* cs)
{
	ExtFunctionType t = extInfo.getType(cs->getCalledFunction());

	Value* retVal = cs->getInstruction();
	Variable* dstVar = isa<PointerType>(retVal->getType()) ? ptrToVariable(retVal) : NULL;

	switch (t)
	{
		case EFT_REALLOC:      // like L_A0 if arg0 is a non-null ptr, else ALLOC
			if (isa<ConstantPointerNull>(cs->getArgument(0)))
			{
				if (dstVar == NULL)
					break;
				
				allocHeap(dstVar, cs);
				break;
			}
			// fall through
		case EFT_L_A0:         // copies arg0, arg1, or arg2 into LHS
		case EFT_L_A1:
		case EFT_L_A2:
		case EFT_L_A8:
		{
			if (dstVar == NULL)
				break;
			unsigned idx = 0;
			switch (t)
			{
				case EFT_L_A1: idx= 1; break;
				case EFT_L_A2: idx= 2; break;
				case EFT_L_A8: idx= 8; break;
			}
			Value* src= cs->getArgument(idx);
			Variable* srcVar = ptrToVariable(src);
			if (srcVar != NULL)
				copyPtr(dstVar, srcVar);
			else
				copyPtr(dstVar, variableFactory.getPointerToUnknown());
			break;
		}		
		case EFT_L_A0__A0R_A1R:		// copies the data that arg1 points to into the location arg0 points to; note that several fields may be copied at once if both point to structs. Returns arg0.
		{
			Value* arg0 = cs->getArgument(0);
			Value* arg1 = cs->getArgument(1);
			if (arg0 != NULL && arg1 != NULL);
				copyMem(arg0, arg1, dstVar);
			break;
		}
		case EFT_A1R_A0R:      //copies *arg0 into *arg1, with non-ptr return
		{
			Value* arg0 = cs->getArgument(0);
			Value* arg1 = cs->getArgument(1);
			if (arg0 != NULL && arg1 != NULL);
				copyMem(arg1, arg0, NULL);
			break;
		}
		case EFT_A3R_A1R_NS:   //copies *arg1 into *arg3 (non-struct copy only)
		{
			Value* arg1 = cs->getArgument(1);
			Value* arg3 = cs->getArgument(3);
			if (arg1 != NULL && arg3 != NULL);
				copyMem(arg3, arg1, dstVar, true);
			break;
		}
		case EFT_A1R_A0:       //stores arg0 into *arg1
		{
			Value* arg0 = cs->getArgument(0);
			Value* arg1 = cs->getArgument(1);
			Variable* arg0Var = ptrToVariable(arg0);
			Variable* arg1Var = ptrToVariable(arg1);
			if (arg0Var != NULL && arg1Var != NULL);
				doStore(arg1Var, arg0Var);
			break;
		}
		case EFT_A2R_A1:       //stores arg1 into *arg2
		{
			Value* arg1 = cs->getArgument(1);
			Value* arg2 = cs->getArgument(2);
			Variable* arg1Var = ptrToVariable(arg1);
			Variable* arg2Var = ptrToVariable(arg2);
			if (arg1Var != NULL && arg2Var != NULL);
				doStore(arg2Var, arg1Var);
			break;
		}
		case EFT_A4R_A1:       //stores arg1 into *arg4
		{
			Value* arg1 = cs->getArgument(1);
			Value* arg4 = cs->getArgument(4);
			Variable* arg1Var = ptrToVariable(arg1);
			Variable* arg4Var = ptrToVariable(arg4);
			if (arg1Var != NULL && arg4Var != NULL);
				doStore(arg4Var, arg1Var);
			break;
		}
		case EFT_L_A0__A2R_A0: //stores arg0 into *arg2 and returns it
		{
			Value* arg0 = cs->getArgument(0);
			Value* arg2 = cs->getArgument(2);
			Variable* arg0Var = ptrToVariable(arg0);
			Variable* arg2Var = ptrToVariable(arg2);
			
			if (arg0Var != NULL && arg2Var != NULL)
				doStore(arg2Var, arg0Var);
			
			if (dstVar != NULL)
			{
				copyPtr(dstVar, arg0Var);
			}
			
			break;
		}
		case EFT_NOSTRUCT_ALLOC: //like ALLOC but only allocates non-struct data
		{
			if (dstVar == NULL)
				break;
			
			allocHeap(dstVar, cs, true);
			
			break;
		}
		case EFT_ALLOC:        //returns a ptr to a newly allocated object
		{
			if (dstVar == NULL)
				break;
			
			allocHeap(dstVar, cs);
			break;
		}
		case EFT_A0R_NEW:      //stores a pointer to an allocated object in *arg0
		case EFT_A1R_NEW:      //as above, into *arg1, etc.
		case EFT_A2R_NEW:
		case EFT_A4R_NEW:
		case EFT_A11R_NEW:
			assert(false && "EFT_AxR_NEW are not supported yet");
			break;
		case EFT_STAT:         //retval points to an unknown static var X
		case EFT_STAT2:        //ret -> X -> Y (X, Y - external static vars)
			assert(false && "EFT_STAT and EFT_STAT2 are not supported yet");
			break;
		case EFT_NOOP:
		case EFT_OTHER:		// Assume unknown external functions have no influence on pointer
			break;
	}
}

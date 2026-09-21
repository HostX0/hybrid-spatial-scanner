#include "hss/hss_api.h"
int main(void){ HSSConfig c={0}; c.abi_version=HSS_ABI_VERSION;c.struct_size=sizeof c;return c.abi_version!=1; }

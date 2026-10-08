/*
 * This ELF is loaded into the already-running SceShellCore process.  The outer
 * injector prepares the target's syscall filter, so the standalone payload SDK
 * ucred/syscall-filter patch must not run again inside SceShellCore.
 */
int
__wrap___patch_init(void)
{
    return 0;
}

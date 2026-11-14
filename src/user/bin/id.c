/* id: who this shell runs as. */
#include "bs.h"

int main(void)
{
    struct bs_user me;
    char group[BS_USER_NAME_MAX];

    whoami(&me);
    group_name(me.gid, group, sizeof(group));
    printf("uid=%u(%s) gid=%u(%s) home=%s shell=%s\n", me.uid, me.name, me.gid, group, me.home, me.shell);
    return 0;
}

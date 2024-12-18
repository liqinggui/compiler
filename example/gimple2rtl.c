int global_int = 0;

int gimple2rtl(int a, short b, char *p)
{
    int i;
    static int static_sum;

    int array[2] = {0, 1};
    static_sum = a;

    for (i = global_int; i < b; ++i) {
        int j = *p;
        static_sum = static_sum + j + array[i];
        if (static_sum > 1000) goto Label_RET;
    }

Label_RET:
    return static_sum;
}
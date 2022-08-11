import math
import random


def get_rand_logspace(start, stop, n):
    assert (0 < start)
    assert (start < stop)
    log2_start = math.log2(start)
    log2_stop = math.log2(stop)
    log2_range = []
    for i in range(n-2):
        x = random.random()*(log2_stop-log2_start)+log2_start
        log2_range = log2_range+[x]
    list.sort(log2_range)
    # print(log2_range)
    out = [start]
    for l in log2_range:
        x = math.floor(math.pow(2, l)+0.5)
        if (x > out[-1]):
            out = out+[x]
            continue
        x = out[-1]+1
        if (x < stop):
            out = out+[x]
        else:
            break
    if (out[-1]+1 < stop):
        out = out+[stop-1]
    return out

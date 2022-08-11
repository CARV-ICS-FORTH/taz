import itertools

# https://stackoverflow.com/questions/5228158/cartesian-product-of-a-dionary-of-lists


def product_dict(**kwargs):
    keys = kwargs.keys()
    vals = kwargs.values()
    # print(keys)
    # print(vals)
    for instance in itertools.product(*vals):
        yield dict(zip(keys, instance))


def merge_dicts(array_of_dicts, common):
    d = [x.copy() for x in array_of_dicts]
    for x in d:
        x.update(common)
    return d

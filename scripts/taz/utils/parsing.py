

def parse_python_data(data_path, variable_name):
    my_globals = {}
    my_locals = {}
    try:
        exec(open(data_path).read(), my_globals,
             my_locals)
        # print(f" globals:{my_globals.keys()}  locals:{my_locals.keys()}")
        assert (variable_name in my_locals)
        return my_locals[variable_name]
    except Exception as e:
        e.args
        print(
            f'caught {type(e)} args:{e.args} traceback: {e.with_traceback(None)}')
        return None

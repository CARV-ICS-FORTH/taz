import pathlib
import os
import sys
import numpy as np
import matplotlib.pyplot as plt
import fbs.taz.profile.Matrix as f_matrix
import fbs.taz.profile.Property as f_property
import flatbuffers


def _convert_to_log(x : np.array ) : 
    log2_x = np.zeros(x.shape, dtype='float64') 
    nonzero = x>0
    np.log2(x,out=log2_x,where=nonzero)
    #print(f"1:{log2_x=}")
    np.floor(log2_x ,out=log2_x,where=nonzero)
    #print(f"2:{log2_x=}")
    ilog2_x=log2_x.astype('int64')
    #print(f"3:{ilog2_x=}")
    large_x= x>=4
    #print(f"3b:{large_x=}")
    y=x.copy()
    np.right_shift( x, ilog2_x - 2, out=y, where=large_x)
    #print(f"4:{x=}")
    np.add( y, (ilog2_x-2) * 4, out=y, where= large_x )    
    return y
    
def _convert_from_log(x : np.array ):
    log2_x = np.right_shift( x, 2)
    #print(f"1:{log2_x=}")
    large_x=  log2_x!=0
    #print(f"2:{large_x=}")
    y = x & 3
    #print(f"3:{y=}")
    np.add( y, 4, out=y, where= large_x )    
    #print(f"5:{y=}")
    np.left_shift( y, log2_x-1, out=y, where= large_x )
    return y

def show_log(max_x):
    x=np.arange(max_x)
    print(f"uint64_t to_log_output[{max_x}]={{{','.join([ str(x) for x in _convert_to_log(x)])}}};")
    xx=np.arange(200)
    print(f"uint64_t from_log_output[200]={{{','.join([ str(x) for x in _convert_from_log(xx)])}}};")
    plt.plot(_convert_from_log(_convert_to_log(x)))
    plt.show()

def fbs_to_property_map(obj):
    d={}
    for i in range(obj.PropertiesLength()):
        p=obj.Properties(i)
        name=p.Name().decode("utf-8")
        if not p.VIntsIsNone():
            d[name]=p.VIntsAsNumpy()
        elif not p.VFloatsIsNone():
            d[name]=p.VFloatsAsNumpy()
        elif not p.VStrIsNone():
            d[name] = [ p.VStr(j).decode("utf-8") for j in range(p.VStrLength())]
    return d

def property_map_to_fbs(builder,map):
    vec=[]
    for key,value in d.items():
        f_property.Start(builder)
        if type(value) is str :
            f_property.AddVStr([value]) 
        elif type(value) is int :
            f_property.AddInts([value]) 
        elif  type(value) is float :
            f_property.AddFloats([value]) 
        elif type(value) is list :
            #print(f"{fragment_name=} {key=} {value=}")
            if len(value)==0 :
                None #Null property
            elif type(value[0]) is str:
                f_property.AddVStr(value)
            elif type(value[0]) is int :
                f_property.AddVInts(value)
            else:
                f_property.AddVFloats(value)
        else:
            print(f"Value type is {type(value)} -> {value}")
            sys.exit(-1)
        vec.append(f_property.End(builder))
    return builder.CreateVector(vec)

class Matrix:

    def __init__(self):
        self.np_array = None
        self.name = ""
        self.properties = {}

    def read(self, filename ):
        if not os.path.exists(filename):
            print(f"Could not read matrix file <{filename}>. abort!")
            sys.exit(-1)
 
        buf = open(filename, 'rb').read()
        buf = bytearray(buf)
        fbs_matrix = f_matrix.Matrix.GetRootAs(buf, 0)
        self.name = fbs_matrix.Name()
        nrows= fbs_matrix.Nrows()
        ncols= fbs_matrix.Ncols()

        #print(f"{self.name=} {nrows=} {ncols=} {fbs_matrix.ExactDataIsNone()=} {fbs_matrix.ApproxDataIsNone()=} {fbs_matrix.ApproxDataAsNumpy()=} {fbs_matrix.PropertiesLength()=}")

        #Now, let us retrieve actual data
        mode = None
        if fbs_matrix.ExactDataIsNone():
             assert(not fbs_matrix.ApproxDataIsNone())
             x= fbs_matrix.ApproxDataAsNumpy().reshape((nrows,ncols)).astype('int64')
             #print(x[:16,:16])
             self.np_array = _convert_from_log(x).reshape((nrows,ncols))
             #print(self.np_array[:16,:16])
             mode = "APPROX"
        else :
             assert(fbs_matrix.ApproxDataIsNone())
             self.np_array =fbs_matrix.ExactDataAsNumpy().reshape((nrows,ncols)).astype('int64')
             mode = "EXACT"

        
        self.properties = fbs_to_property_map(fbs_matrix)

        # print(f"Successfully read {mode} matrix {self.name} of size {nrows}x{ncols}")


    def write(self, mode, filename ):
        b = flatbuffers.Builder(0)
        fbs_name=b.CreateString(self.name)
        fbs_data=None
        if mode == 'EXACT':
            fbs_data=b.CreateNumpyVector(self.np_array.reshape(-1))
        else :
            assert(mode== 'APPROX')
            x=self.np_array.reshape(-1)
            #print(x.reshape(16,-1))
            y= _convert_to_log(x).astype('b')
            #y2=y.reshape(16,16)
            #print(f"{y2=}")
            fbs_data=b.CreateNumpyVector(y)

        properties_offset=0
        if len(self.properties.keys()) > 0 :
            properties_offset=property_map_to_fbs(b,self.properties)

        matrix.Start(b)
        matrix.AddName(b,fbs_name)
        matrix.AddProperties(b,properties_offset)
        matrix.AddNrows(b,self.np_array.shape[0])
        matrix.AddNcols(b,self.np_array.shape[1])
        if mode == 'EXACT':
            matrix.AddExactData(b,fbs_data)
        else:
             matrix.AddApproxData(b,fbs_data)
        mat=matrix.End(b)

        b.Finish(mat)

        with open(filename, 'wb') as f :
            f.write(b.Output())
        
        # print(f"Successfully write {mode} matrix {self.name} of size {self.np_array.shape[0]}x{self.np_array.shape[1]}")
    

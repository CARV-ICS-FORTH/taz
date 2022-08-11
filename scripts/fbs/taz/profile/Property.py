# automatically generated by the FlatBuffers compiler, do not modify

# namespace: profile

import flatbuffers
from flatbuffers.compat import import_numpy
np = import_numpy()

class Property(object):
    __slots__ = ['_tab']

    @classmethod
    def GetRootAs(cls, buf, offset=0):
        n = flatbuffers.encode.Get(flatbuffers.packer.uoffset, buf, offset)
        x = Property()
        x.Init(buf, n + offset)
        return x

    @classmethod
    def GetRootAsProperty(cls, buf, offset=0):
        """This method is deprecated. Please switch to GetRootAs."""
        return cls.GetRootAs(buf, offset)
    # Property
    def Init(self, buf, pos):
        self._tab = flatbuffers.table.Table(buf, pos)

    # Property
    def Name(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(4))
        if o != 0:
            return self._tab.String(o + self._tab.Pos)
        return None

    # Property
    def VInts(self, j):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(6))
        if o != 0:
            a = self._tab.Vector(o)
            return self._tab.Get(flatbuffers.number_types.Int64Flags, a + flatbuffers.number_types.UOffsetTFlags.py_type(j * 8))
        return 0

    # Property
    def VIntsAsNumpy(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(6))
        if o != 0:
            return self._tab.GetVectorAsNumpy(flatbuffers.number_types.Int64Flags, o)
        return 0

    # Property
    def VIntsLength(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(6))
        if o != 0:
            return self._tab.VectorLen(o)
        return 0

    # Property
    def VIntsIsNone(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(6))
        return o == 0

    # Property
    def VFloats(self, j):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(8))
        if o != 0:
            a = self._tab.Vector(o)
            return self._tab.Get(flatbuffers.number_types.Float64Flags, a + flatbuffers.number_types.UOffsetTFlags.py_type(j * 8))
        return 0

    # Property
    def VFloatsAsNumpy(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(8))
        if o != 0:
            return self._tab.GetVectorAsNumpy(flatbuffers.number_types.Float64Flags, o)
        return 0

    # Property
    def VFloatsLength(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(8))
        if o != 0:
            return self._tab.VectorLen(o)
        return 0

    # Property
    def VFloatsIsNone(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(8))
        return o == 0

    # Property
    def VStr(self, j):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(10))
        if o != 0:
            a = self._tab.Vector(o)
            return self._tab.String(a + flatbuffers.number_types.UOffsetTFlags.py_type(j * 4))
        return ""

    # Property
    def VStrLength(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(10))
        if o != 0:
            return self._tab.VectorLen(o)
        return 0

    # Property
    def VStrIsNone(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(10))
        return o == 0

def PropertyStart(builder):
    builder.StartObject(4)

def Start(builder):
    PropertyStart(builder)

def PropertyAddName(builder, name):
    builder.PrependUOffsetTRelativeSlot(0, flatbuffers.number_types.UOffsetTFlags.py_type(name), 0)

def AddName(builder, name):
    PropertyAddName(builder, name)

def PropertyAddVInts(builder, vInts):
    builder.PrependUOffsetTRelativeSlot(1, flatbuffers.number_types.UOffsetTFlags.py_type(vInts), 0)

def AddVInts(builder, vInts):
    PropertyAddVInts(builder, vInts)

def PropertyStartVIntsVector(builder, numElems):
    return builder.StartVector(8, numElems, 8)

def StartVIntsVector(builder, numElems: int) -> int:
    return PropertyStartVIntsVector(builder, numElems)

def PropertyAddVFloats(builder, vFloats):
    builder.PrependUOffsetTRelativeSlot(2, flatbuffers.number_types.UOffsetTFlags.py_type(vFloats), 0)

def AddVFloats(builder, vFloats):
    PropertyAddVFloats(builder, vFloats)

def PropertyStartVFloatsVector(builder, numElems):
    return builder.StartVector(8, numElems, 8)

def StartVFloatsVector(builder, numElems: int) -> int:
    return PropertyStartVFloatsVector(builder, numElems)

def PropertyAddVStr(builder, vStr):
    builder.PrependUOffsetTRelativeSlot(3, flatbuffers.number_types.UOffsetTFlags.py_type(vStr), 0)

def AddVStr(builder, vStr):
    PropertyAddVStr(builder, vStr)

def PropertyStartVStrVector(builder, numElems):
    return builder.StartVector(4, numElems, 4)

def StartVStrVector(builder, numElems: int) -> int:
    return PropertyStartVStrVector(builder, numElems)

def PropertyEnd(builder):
    return builder.EndObject()

def End(builder):
    return PropertyEnd(builder)

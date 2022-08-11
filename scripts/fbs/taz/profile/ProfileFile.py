# automatically generated by the FlatBuffers compiler, do not modify

# namespace: profile

import flatbuffers
from flatbuffers.compat import import_numpy
np = import_numpy()

class ProfileFile(object):
    __slots__ = ['_tab']

    @classmethod
    def GetRootAs(cls, buf, offset=0):
        n = flatbuffers.encode.Get(flatbuffers.packer.uoffset, buf, offset)
        x = ProfileFile()
        x.Init(buf, n + offset)
        return x

    @classmethod
    def GetRootAsProfileFile(cls, buf, offset=0):
        """This method is deprecated. Please switch to GetRootAs."""
        return cls.GetRootAs(buf, offset)
    @classmethod
    def ProfileFileBufferHasIdentifier(cls, buf, offset, size_prefixed=False):
        return flatbuffers.util.BufferHasIdentifier(buf, offset, b"\x54\x41\x5A\x50", size_prefixed=size_prefixed)

    # ProfileFile
    def Init(self, buf, pos):
        self._tab = flatbuffers.table.Table(buf, pos)

    # ProfileFile
    def Header(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(4))
        if o != 0:
            x = self._tab.Indirect(o + self._tab.Pos)
            from taz.profile.ProfileHeader import ProfileHeader
            obj = ProfileHeader()
            obj.Init(self._tab.Bytes, x)
            return obj
        return None

    # ProfileFile
    def Steps(self, j):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(6))
        if o != 0:
            x = self._tab.Vector(o)
            x += flatbuffers.number_types.UOffsetTFlags.py_type(j) * 4
            x = self._tab.Indirect(x)
            from taz.profile.TraceStep import TraceStep
            obj = TraceStep()
            obj.Init(self._tab.Bytes, x)
            return obj
        return None

    # ProfileFile
    def StepsLength(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(6))
        if o != 0:
            return self._tab.VectorLen(o)
        return 0

    # ProfileFile
    def StepsIsNone(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(6))
        return o == 0

    # ProfileFile
    def Instances(self, j):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(8))
        if o != 0:
            x = self._tab.Vector(o)
            x += flatbuffers.number_types.UOffsetTFlags.py_type(j) * 4
            x = self._tab.Indirect(x)
            from taz.profile.PrimitiveInstance import PrimitiveInstance
            obj = PrimitiveInstance()
            obj.Init(self._tab.Bytes, x)
            return obj
        return None

    # ProfileFile
    def InstancesLength(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(8))
        if o != 0:
            return self._tab.VectorLen(o)
        return 0

    # ProfileFile
    def InstancesIsNone(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(8))
        return o == 0

def ProfileFileStart(builder):
    builder.StartObject(3)

def Start(builder):
    ProfileFileStart(builder)

def ProfileFileAddHeader(builder, header):
    builder.PrependUOffsetTRelativeSlot(0, flatbuffers.number_types.UOffsetTFlags.py_type(header), 0)

def AddHeader(builder, header):
    ProfileFileAddHeader(builder, header)

def ProfileFileAddSteps(builder, steps):
    builder.PrependUOffsetTRelativeSlot(1, flatbuffers.number_types.UOffsetTFlags.py_type(steps), 0)

def AddSteps(builder, steps):
    ProfileFileAddSteps(builder, steps)

def ProfileFileStartStepsVector(builder, numElems):
    return builder.StartVector(4, numElems, 4)

def StartStepsVector(builder, numElems: int) -> int:
    return ProfileFileStartStepsVector(builder, numElems)

def ProfileFileAddInstances(builder, instances):
    builder.PrependUOffsetTRelativeSlot(2, flatbuffers.number_types.UOffsetTFlags.py_type(instances), 0)

def AddInstances(builder, instances):
    ProfileFileAddInstances(builder, instances)

def ProfileFileStartInstancesVector(builder, numElems):
    return builder.StartVector(4, numElems, 4)

def StartInstancesVector(builder, numElems: int) -> int:
    return ProfileFileStartInstancesVector(builder, numElems)

def ProfileFileEnd(builder):
    return builder.EndObject()

def End(builder):
    return ProfileFileEnd(builder)

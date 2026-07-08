import pytest
from pysjef import Node


class Base(Node):
    def __eq__(self, other):
        equal = self.nodename == other.nodename
        equal = equal and self.attributes["id"] == other.attributes["id"]
        equal = equal and len(self.children) == len(other.children)
        if not equal:
            return equal
        for i in range(len(self.children)):
            equal = equal and self.children[i] == other.children[i]
        return equal


class Leaf(Base):
    def __init__(self, **kwargs):
        super().__init__()
        if len(kwargs) == 0:
            return
        parent = kwargs.get("parent")
        id = kwargs.get("id")
        self.nodename = "Leaf"
        self.parent = parent
        self.attributes["id"] = id


class LevelB(Base):
    def __init__(self, **kwargs):
        super().__init__()
        if len(kwargs) == 0:
            return
        parent = kwargs.get("parent")
        idB = kwargs.get("idB")
        idsLeaf = kwargs.get("idsLeaf")
        self.nodename = "B"
        self.parent = parent
        self.attributes["id"] = idB
        for id in idsLeaf:
            self.children.append(Leaf(parent=self, id=idB + id))


class LevelA(Base):
    def __init__(self, **kwargs):
        super().__init__()
        self.is_project = True
        if len(kwargs) == 0:
            return
        parent = kwargs.get("parent")
        idA = kwargs.get("idA")
        idsB = kwargs.get("idsB")
        idsLeaf = kwargs.get("idsLeaf")
        self.nodename = "A"
        self.parent = parent
        self.attributes["id"] = idA
        for id in idsB:
            self.children.append(
                LevelB(parent=self, idB=idA + id, idsLeaf=idsLeaf))


class Root(Base):
    def __init__(self, **kwargs):
        super().__init__()
        if len(kwargs) == 0:
            return
        idsA = kwargs.get("idsA")
        idsB = kwargs.get("idsB")
        idsLeaf = kwargs.get("idsLeaf")
        self.nodename = "root"
        self.attributes["id"] = ""
        for id in idsA:
            self.children.append(
                LevelA(parent=self, idA=id, idsB=idsB, idsLeaf=idsLeaf))


@pytest.fixture
def tree():
    """
                                      root
          |............................|..........................|
          1                            2                          3
  |.......|.......|           |.......|.......|           |.......|.........|
  1a      1b      1c          2a      2b      2c          3a      3b        3c
|...|   |...|   |...|     |....|   |...|    |...|       |...|   |...|   |...|
1ax 1az 1bx 1bz 1cx 1cbz  2ax  2az 2bx 2bz  2cx 2cz     3ax 3az 3bx 3bz 3cx 3cz
    """
    return Root(idsA=["1", "2", "3"], idsB=["a", "b", "c"], idsLeaf=["x", "z"])
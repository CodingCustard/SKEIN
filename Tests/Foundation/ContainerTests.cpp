#include <Skein/Foundation/FlatMap.h>
#include <Skein/Foundation/IntrusiveList.h>
#include <Skein/Foundation/RingBuffer.h>
#include <Skein/Foundation/SmallVector.h>

namespace
{
    struct ListValue final
    {
        int Value = 0;
        Skein::IntrusiveListNode Node;
    };

    using ValueList = Skein::IntrusiveList<ListValue, &ListValue::Node>;
}

int main()
{
    using namespace Skein;

    SmallVector<int, 2> values;
    if (!values.IsInline() || !values.PushBack(1) || !values.PushBack(2) ||
        !values.IsInline() || !values.PushBack(3) || values.IsInline() ||
        values.Size() != 3 || values[0] != 1 || values[2] != 3)
    {
        return 1;
    }

    SmallVector<int, 2> movedValues{std::move(values)};
    if (movedValues.Size() != 3 || values.Size() != 0 || !values.IsInline() ||
        !movedValues.PopBack() || movedValues.Back() != 2)
    {
        return 2;
    }

    FlatMap<int, int> map;
    Result<bool> insertedThree = map.InsertOrAssign(3, 30);
    Result<bool> insertedOne = map.InsertOrAssign(1, 10);
    Result<bool> updatedThree = map.InsertOrAssign(3, 31);
    if (!insertedThree || !insertedThree.Value() || !insertedOne ||
        !insertedOne.Value() || !updatedThree || updatedThree.Value() ||
        map.Size() != 2 || map.Find(3) == nullptr || *map.Find(3) != 31 ||
        !map.Erase(1) || map.Erase(1))
    {
        return 3;
    }

    RingBuffer<int, 3> ring;
    if (!ring.Push(10) || !ring.Push(20) || !ring.Push(30) || ring.Push(40))
    {
        return 4;
    }
    Result<int> first = ring.Pop();
    if (!first || first.Value() != 10 || !ring.Push(40))
    {
        return 5;
    }
    Result<int> second = ring.Pop();
    Result<int> third = ring.Pop();
    Result<int> fourth = ring.Pop();
    if (!second || second.Value() != 20 || !third || third.Value() != 30 ||
        !fourth || fourth.Value() != 40 || ring.Pop())
    {
        return 6;
    }

    ListValue one{1, {}};
    ListValue two{2, {}};
    ListValue three{3, {}};
    ValueList list;
    if (!list.PushBack(one) || !list.PushBack(two) || !list.PushBack(three) ||
        list.PushBack(two) || list.Size() != 3)
    {
        return 7;
    }

    int sum = 0;
    for (const ListValue& value : list)
    {
        sum += value.Value;
    }
    if (sum != 6 || !list.Remove(two) || two.Node.IsLinked() ||
        list.Remove(two) || list.Size() != 2)
    {
        return 8;
    }

    list.Clear();
    if (!list.IsEmpty() || one.Node.IsLinked() || three.Node.IsLinked())
    {
        return 9;
    }

    return 0;
}

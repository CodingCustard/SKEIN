#include <Skein/Foundation/Name.h>

#include <new>
#include <system_error>

namespace Skein
{
    Result<Name> NameTable::Intern(const StringView text) try
    {
        SKEIN_TRY(ValidateUtf8(text));
        if (text.empty())
        {
            return Unexpected{Error{ErrorCode::InvalidArgument, "name cannot be empty"}};
        }

        const u64 hash = StableHash64(text);
        std::scoped_lock lock{m_mutex};
        String* const existing = m_names.Find(hash);
        if (existing != nullptr)
        {
            if (*existing != text)
            {
                return Unexpected{Error{ErrorCode::AlreadyExists, "stable name hash collision"}};
            }
            return Name{hash};
        }

        Result<bool> inserted = m_names.InsertOrAssign(hash, String{text});
        if (!inserted)
        {
            return Unexpected{inserted.ErrorValue()};
        }
        return Name{hash};
    }
    catch (const std::bad_alloc&)
    {
        return Unexpected{Error{ErrorCode::OutOfMemory, "name table allocation failed"}};
    }
    catch (const std::system_error&)
    {
        return Unexpected{Error{ErrorCode::Internal, "name table lock failed"}};
    }

    Result<String> NameTable::Resolve(const Name name) const try
    {
        if (!name.IsValid())
        {
            return Unexpected{Error{ErrorCode::InvalidArgument, "name is invalid"}};
        }

        std::scoped_lock lock{m_mutex};
        const String* const existing = m_names.Find(name.Value());
        if (existing == nullptr)
        {
            return Unexpected{Error{ErrorCode::NotFound, "name is not interned"}};
        }
        return *existing;
    }
    catch (const std::bad_alloc&)
    {
        return Unexpected{Error{ErrorCode::OutOfMemory, "name copy allocation failed"}};
    }
    catch (const std::system_error&)
    {
        return Unexpected{Error{ErrorCode::Internal, "name table lock failed"}};
    }

    std::size_t NameTable::Size() const
    {
        std::scoped_lock lock{m_mutex};
        return m_names.Size();
    }

    void NameTable::Clear()
    {
        std::scoped_lock lock{m_mutex};
        m_names.Clear();
    }
}

#include <Skein/Foundation/Path.h>
#include <Skein/Foundation/SmallVector.h>

#include <cctype>
#include <new>

namespace Skein
{
    Result<NormalizedPath> NormalizePath(const StringView path) try
    {
        SKEIN_TRY(ValidateUtf8(path));
        if (path.find('\0') != StringView::npos)
        {
            return Unexpected{Error{ErrorCode::InvalidArgument, "path contains a null character"}};
        }

        String canonical{path};
        for (char& character : canonical)
        {
            if (character == '\\')
            {
                character = '/';
            }
        }

        enum class Prefix
        {
            Relative,
            Root,
            Drive,
            Unc
        };

        Prefix prefix = Prefix::Relative;
        bool absolute = false;
        std::size_t cursor = 0;
        char driveLetter = '\0';

        if (canonical.size() >= 2 &&
            std::isalpha(static_cast<unsigned char>(canonical[0])) != 0 &&
            canonical[1] == ':')
        {
            if (canonical.size() < 3 || canonical[2] != '/')
            {
                return Unexpected{Error{
                    ErrorCode::InvalidArgument,
                    "drive-relative paths are not supported"}};
            }
            prefix = Prefix::Drive;
            absolute = true;
            driveLetter = static_cast<char>(
                std::toupper(static_cast<unsigned char>(canonical[0])));
            cursor = 3;
        }
        else if (canonical.starts_with("//"))
        {
            prefix = Prefix::Unc;
            absolute = true;
            cursor = 2;
        }
        else if (canonical.starts_with('/'))
        {
            prefix = Prefix::Root;
            absolute = true;
            cursor = 1;
        }

        SmallVector<String, 16> segments;
        while (cursor <= canonical.size())
        {
            const std::size_t separator = canonical.find('/', cursor);
            const std::size_t end = separator == String::npos ? canonical.size() : separator;
            const StringView segment{canonical.data() + cursor, end - cursor};

            if (!segment.empty() && segment != ".")
            {
                if (segment == "..")
                {
                    const std::size_t rootDepth = prefix == Prefix::Unc ? 2 : 0;
                    if (segments.Size() > rootDepth && segments.Back() != "..")
                    {
                        (void)segments.PopBack();
                    }
                    else if (absolute)
                    {
                        return Unexpected{Error{
                            ErrorCode::InvalidArgument,
                            "path traverses above its root"}};
                    }
                    else
                    {
                        Result<String*> appended = segments.EmplaceBack(segment);
                        if (!appended)
                        {
                            return Unexpected{appended.ErrorValue()};
                        }
                    }
                }
                else
                {
                    Result<String*> appended = segments.EmplaceBack(segment);
                    if (!appended)
                    {
                        return Unexpected{appended.ErrorValue()};
                    }
                }
            }

            if (separator == String::npos)
            {
                break;
            }
            cursor = separator + 1;
        }

        if (prefix == Prefix::Unc && segments.Size() < 2)
        {
            return Unexpected{Error{
                ErrorCode::InvalidArgument,
                "UNC paths require a server and share"}};
        }

        String normalized;
        if (prefix == Prefix::Drive)
        {
            normalized.push_back(driveLetter);
            normalized.append(":/");
        }
        else if (prefix == Prefix::Unc)
        {
            normalized = "//";
        }
        else if (prefix == Prefix::Root)
        {
            normalized = "/";
        }

        for (const String& segment : segments)
        {
            if (!normalized.empty() && normalized.back() != '/')
            {
                normalized.push_back('/');
            }
            normalized.append(segment);
        }

        if (prefix == Prefix::Relative && normalized.empty())
        {
            normalized = ".";
        }

        return NormalizedPath{std::move(normalized), absolute};
    }
    catch (const std::bad_alloc&)
    {
        return Unexpected{Error{ErrorCode::OutOfMemory, "path normalization allocation failed"}};
    }
}

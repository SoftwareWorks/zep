#include <cctype>
#include <cstdint>
#include <cstdlib>

#include "buffer.h"

#include <algorithm>
#include <regex>

namespace
{
// A VIM-like definition of a word.  Actually, in Vim this can be changed, but this editor
// assumes a word is alphanumberic or underscore for consistency
inline bool IsWordChar(const char ch) { return std::isalnum(ch) || ch == '_'; }
inline bool IsNonWordChar(const char ch) { return (!IsWordChar(ch) && !std::isspace(ch)); }
inline bool IsWORDChar(const char ch) { return std::isgraph(ch); }
inline bool IsNonWORDChar(const char ch) { return !IsWORDChar(ch) && !std::isspace(ch); }
inline bool IsSpace(const char ch) { return std::isspace(ch); }
}

namespace PicoVim
{

const char* Msg_Buffer = "Buffer";
PicoVimBuffer::PicoVimBuffer(PicoVimEditor& editor, const std::string& strName)
    : PicoVimComponent(editor),
    m_threadPool(),
    m_strName(strName)
{
}

PicoVimBuffer::~PicoVimBuffer()
{
    // TODO: Ensure careful shutdown
    StopThreads();
}

void PicoVimBuffer::StopThreads(bool immediate)
{
    // Stop the thread, wait for it
    if (immediate)
    {
        m_stop = true;
    }
    if (m_lineCountResult.valid())
    {
        m_lineCountResult.get();
    }
    m_stop = false;
}

void PicoVimBuffer::Notify(std::shared_ptr<PicoVimMessage> message)
{

}

BufferLocation PicoVimBuffer::LocationFromOffsetByChars(const BufferLocation& location, long offset) const
{
    // Walk and find multibyte.
    // TODO: Only handles CR/LF
    long dir = offset > 0 ? 1 : -1;
    char firstCR = '\r';
    char nextCR = '\n';
    if (dir < 0)
    {
        std::swap(firstCR, nextCR);
    }

    // TODO: This can be cleaner(?)
    long current = location;
    for (long i = 0; i < std::abs(offset); i++)
    {
        // If walking back, move back before looking at char
        if (dir == -1)
            current += dir;

        if (current >= m_buffer.size())
            break;

        if (m_buffer[current] == firstCR)
        {
            if ((current + dir) >= m_buffer.size())
            {
                break;
            }
            if (m_buffer[current + dir] == nextCR)
            {
                current += dir;
            }
        }

        // If walking forward, post append
        if (dir == 1)
        {
            current += dir;
        }
    }

    return LocationFromOffset(current);
}

BufferLocation PicoVimBuffer::LocationFromOffset(const BufferLocation& location, long offset) const
{
    return LocationFromOffset(location + offset);
}

long PicoVimBuffer::LineFromOffset(long offset) const
{
    auto itrLine = std::lower_bound(m_lineEnds.begin(), m_lineEnds.end(), offset);
    if (itrLine != m_lineEnds.end() && offset >= *itrLine)
    {
        itrLine++;
    }
    long line = long(itrLine - m_lineEnds.begin());
    line = std::min(std::max(0l, line), long(m_lineEnds.size() - 1));
    return line;
}

BufferLocation PicoVimBuffer::LocationFromOffset(long offset) const
{
    return BufferLocation{ offset };
}

BufferLocation PicoVimBuffer::Search(const std::string& str, BufferLocation start, SearchDirection dir, BufferLocation end) const
{
    return BufferLocation{ 0 };
}

// Given a stream of ___AAA__BBB
// We return markers for the start of the first block, beyond the first block, and the second
// i.e. we 'find' AAA and BBB, and remember if there are spaces between them
// This enables us to implement various block motions
BufferBlock PicoVimBuffer::GetBlock(uint32_t searchType, BufferLocation start, SearchDirection dir) const
{
    BufferBlock ret;
    ret.blockSearchPos = start;

    BufferLocation end;
    BufferLocation begin;
    end = BufferLocation{ long(m_buffer.size()) };
    begin = BufferLocation{ 0 };

    auto itrBegin = m_buffer.begin() + begin;
    auto itrEnd = m_buffer.begin() + end;
    auto itrCurrent = m_buffer.begin() + start;

    auto pIsBlock = IsWORDChar;
    auto pIsNotBlock = IsNonWORDChar;
    if (searchType & SearchType::AlphaNumeric)
    {
        pIsBlock = IsWordChar;
        pIsNotBlock = IsNonWordChar;
    }

    // Set the search pos
    ret.blockSearchPos = LocationFromOffset(long(itrCurrent - m_buffer.begin()));

    auto inc = (dir == SearchDirection::Forward) ? 1 : -1;
    if (inc == -1)
    {
        std::swap(itrBegin, itrEnd);
    }
    ret.direction = inc;

    ret.spaceBefore = false;
    ret.spaceBetween = false;

    while ((itrCurrent != itrBegin) &&
        std::isspace(*itrCurrent))
    {
        itrCurrent -= inc;
    }
    if (itrCurrent != itrBegin)
    {
        itrCurrent += inc;
    }
    ret.spaceBeforeStart = LocationFromOffset(long(itrCurrent - m_buffer.begin()));

    // Skip the initial spaces; they are not part of the block
    itrCurrent = m_buffer.begin() + start;
    while (itrCurrent != itrEnd &&
        (std::isspace(*itrCurrent)))
    {
        ret.spaceBefore = true;
        itrCurrent += inc;
    }

    auto GetBlockChecker = [&](const char ch) 
    {
        if (pIsBlock(ch))
            return pIsBlock;
        else 
            return pIsNotBlock;
    };

    // Find the right start block type
    auto pCheck = GetBlockChecker(*itrCurrent);
    ret.startOnBlock = (pCheck == pIsBlock) ? true : false;

    // Walk backwards to the start of the block
    while (itrCurrent != itrBegin &&
        (pCheck(*itrCurrent)))
    {
        itrCurrent -= inc;
    }
    if (!pCheck(*itrCurrent))  // Note this also handles where we couldn't walk back any further
    {
        itrCurrent += inc;
    }

    // Record start
    ret.firstBlock = LocationFromOffset(long(itrCurrent - m_buffer.begin()));
   
    // Walk forwards to the end of the block
    while (itrCurrent != itrEnd &&
        (pCheck(*itrCurrent)))
    {
        itrCurrent += inc;
    }

    // Record end
    ret.firstNonBlock = LocationFromOffset(long(itrCurrent - m_buffer.begin()));

    // Skip the next spaces; they are not part of the block
    while (itrCurrent != itrEnd &&
        (std::isspace(*itrCurrent)))
    {
        ret.spaceBetween = true;
        itrCurrent += inc;
    }

    ret.secondBlock = LocationFromOffset(long(itrCurrent - m_buffer.begin()));

    // Get to the end of the second non block
    pCheck = GetBlockChecker(*itrCurrent);
    while (itrCurrent != itrEnd &&
        (pCheck(*itrCurrent)))
    {
        itrCurrent += inc;
    }

    ret.secondNonBlock = LocationFromOffset(long(itrCurrent - m_buffer.begin()));
    
    return ret;
}

// TODO:
// We could count line ends on different threads and collate them for speed
void PicoVimBuffer::FindLineEnds()
{
    m_processedLine = 0;
    m_lineEnds.clear();

    long offset = 0;
    long lastEnd = 0;

    auto itrEnd = m_buffer.end();
    auto itrBegin = m_buffer.begin();
    auto itr = m_buffer.begin();

    std::string lineEndSymbols("\r\n");
    while (itr != itrEnd)
    {
        if (m_stop)
            break;

        // Get to first point after "\n" or "\r\n"
        // That's the point just after the end of the current line
        itr = m_buffer.find_first_of(itr, itrEnd, lineEndSymbols.begin(), lineEndSymbols.end());
        if (itr != itrEnd)
        {
            if (*itr == '\r')
                itr++;
            if (itr != itrEnd &&
                *itr == '\n')
            {
                itr++;
            }
        }

        // Note; if itrEnd then we store a line end for the '0' at the end of the buffer.
        // This makes a buffer of 0 length have a single 0-length line
        // Lock the line end buffer and update 
        std::unique_lock<std::shared_mutex> lineLock(m_lineEndsLock);
        m_lineEnds.push_back(long(itr - itrBegin));
        lineLock.unlock();

        // Update the atomic line counter so clients can see where we are up to.
        m_processedLine = long(m_lineEnds.size());
    }
}

long PicoVimBuffer::ClampLine(long line) const
{
    line = std::min(line, GetProcessedLine());
    line = std::max(line, 0l);
    return line;
}

BufferLocation PicoVimBuffer::Clamp(BufferLocation in) const
{
    in = std::min(in, BufferLocation(m_buffer.size() - 1));
    in = std::max(in, BufferLocation(0));
    return in;
}

// Method for querying the beginning and end of a line
bool PicoVimBuffer::GetLineOffsets(const long line, long& lineStart, long& lineEnd) const
{
    // Reader lock
    std::shared_lock<std::shared_mutex> lineLock(m_lineEndsLock);

    // No more lines; may not have finished processing
    if (m_lineEnds.size() <= line)
    {
        lineStart = 0;
        lineEnd = 0;
        return false;
    }

    // Find the line bounds - we know the end, find the start from the previous
    lineEnd = m_lineEnds[line];
    lineStart = line == 0 ? 0 : m_lineEnds[line - 1];
    return true;
}

/* TODO:
void PicoVimBuffer::LockRead()
{
    // Wait for line counter
    if (m_lineCountResult.valid())
    {
        m_lineCountResult.get();
    }
}*/


// Replace the buffer buffer with the text 
void PicoVimBuffer::SetText(const std::string& text)
{
    // Stop threads
    StopThreads();

    GetEditor().Broadcast(std::make_shared<BufferMessage>(this,
        BufferMessageType::TextDeleted,
        BufferLocation{ 0 },
        BufferLocation{ long(m_buffer.size()) }));

    // Ensure the buffer always has a '0' - ie. it is size 1, with file end in it!
    if (text.empty())
    {
        m_buffer.clear();
        m_buffer.push_back(0);
    }
    else
    {
        // Update the gap buffer with the text
        m_buffer.insert(m_buffer.begin(), text.begin(), text.end());
        if (m_buffer[m_buffer.size() - 1] != 0)
        {
            m_buffer.push_back(0);
        }
    }

    // Count lines into another gap buffer
    m_processedLine = 0;

    if (GetEditor().GetFlags() & PicoVimEditorFlags::DisableThreads)
    {
        FindLineEnds();
    }
    else
    {
        m_lineCountResult = m_threadPool.enqueue([&]() { FindLineEnds(); });
    }

    GetEditor().Broadcast(std::make_shared<BufferMessage>(this,
        BufferMessageType::TextAdded,
        BufferLocation{ 0 },
        BufferLocation{ long(m_buffer.size()) }));

    // Doc is not dirty
    m_dirty = 0;
}


BufferLocation PicoVimBuffer::GetLinePos(long line, LineLocation location) const
{
    // Clamp the line
    if (m_lineEnds.size() <= line)
    {
        line = long(m_lineEnds.size()) - 1l;
        line = std::max(0l, line);
    }
    else if (line < 0)
    {
        line = 0l;
    }

    BufferLocation ret{ 0 };
    if (m_lineEnds.empty())
    {
        ret = 0;
        return ret;
    }

    auto searchEnd = m_lineEnds[line];
    auto searchStart = 0;
    if (line > 0)
    {
        searchStart = m_lineEnds[line - 1];
    }

    switch (location)
    {
    default:
    case LineLocation::LineBegin:
        ret = searchStart;
        break;

    case LineLocation::LineEnd:
        ret = searchEnd;
        break;

    case LineLocation::LineCRBegin:
    {
        auto loc = std::find_if(m_buffer.begin() + searchStart, m_buffer.end() + searchEnd,
            [&](const utf8& ch)
        {
            if (ch == '\n' || ch == '\r' || ch == 0)
                return true;
            return false;
        });
        ret = long(loc - m_buffer.begin());
    }
    break;

    case LineLocation::LineFirstGraphChar:
    {
        auto loc = std::find_if(m_buffer.begin() + searchStart, m_buffer.end() + searchEnd,
            [&](const utf8& ch) { return ch != 0 && std::isgraph(ch); });
        ret = long(loc - m_buffer.begin());
    }
    break;

    case LineLocation::LineLastNonCR:
    {
        auto begin = std::reverse_iterator<GapBuffer<utf8>::const_iterator>(m_buffer.begin() + searchEnd);
        auto end = std::reverse_iterator<GapBuffer<utf8>::const_iterator>(m_buffer.begin() + searchStart);
        auto loc = std::find_if(begin, end,
            [&](const utf8& ch)
        {
            return ch != '\r' && ch != '\n' && ch != 0;
        });
        if (loc == end)
        {
            ret = searchEnd;
        }
        else
        {
            ret = long(loc.base() - m_buffer.begin() - 1);
        }
    }
    break;

    case LineLocation::LineLastGraphChar:
    {
        auto begin = std::reverse_iterator<GapBuffer<utf8>::const_iterator>(m_buffer.begin() + searchEnd);
        auto end = std::reverse_iterator<GapBuffer<utf8>::const_iterator>(m_buffer.begin() + searchStart);
        auto loc = std::find_if(begin, end,
            [&](const utf8& ch) { return std::isgraph(ch); });
        if (loc == end)
        {
            ret = searchEnd;
        }
        else
        {
            ret = long(loc.base() - m_buffer.begin() - 1);
        }
    }
    break;
    }

    return ret;
}

bool PicoVimBuffer::Insert(const BufferLocation& startOffset, const std::string& str, const BufferLocation& cursorAfter)
{
    if (startOffset > m_buffer.size())
    {
        return false;
    }

    StopThreads();

    BufferLocation changeRange{ long(m_buffer.size()) };

    // We are about to modify this range
    GetEditor().Broadcast(std::make_shared<BufferMessage>(this, BufferMessageType::PreBufferChange, startOffset, changeRange));

    // abcdef\r\nabc<insert>dfdf\r\n
    auto itrLine = std::lower_bound(m_lineEnds.begin(), m_lineEnds.end(), startOffset);;
    if (itrLine != m_lineEnds.end() &&
        *itrLine <= startOffset)
    {
        itrLine++;
    }

    auto itrEnd = str.end();
    auto itrBegin = str.begin();
    auto itr = str.begin();

    std::vector<long> lines;
    std::string lineEndSymbols("\r\n");
    while (itr != itrEnd)
    {
        // Get to first point after "\n" or "\r\n"
        // That's the point just after the end of the current line
        itr = std::find_first_of(itr, itrEnd, lineEndSymbols.begin(), lineEndSymbols.end());
        if (itr != itrEnd)
        {
            if (*itr == '\r')
                itr++;
            if (itr != itrEnd &&
                *itr == '\n')
            {
                itr++;
            }
            lines.push_back(long(itr - itrBegin) + startOffset);
        }
    }

    // Increment the rest of the line ends
    // We make all the remaning line ends bigger by the size of the insertion
    auto itrAdd = itrLine;
    while (itrAdd != m_lineEnds.end())
    {
        *itrAdd += long(str.length());
        itrAdd++;
    }

    if (!lines.empty())
    {
        // Update the atomic line counter so clients can see where we are up to.
        m_processedLine += long(lines.size());
        m_lineEnds.insert(itrLine, lines.begin(), lines.end());
    }

    m_buffer.insert(m_buffer.begin() + startOffset, str.begin(), str.end());

    // This is the range we added (not valid any more in the buffer)
    GetEditor().Broadcast(std::make_shared<BufferMessage>(this, BufferMessageType::TextAdded, startOffset, changeRange, cursorAfter));
    return true;
}

// A fundamental operation - delete a range of characters
// Need to update:
// - m_lineEnds
// - m_processedLine
// - m_buffer (i.e remove chars)
// We also need to inform clients before we change the buffer, and after we delete text with the range we removed.
// This helps them to fix up their data structures without rebuilding.
// Assumption: The buffer always is at least a single line/character of '0', representing file end.
// This makes a few things fall out more easily
bool PicoVimBuffer::Delete(const BufferLocation& startOffset, const BufferLocation& endOffset, const BufferLocation& cursorAfter)
{
    assert(startOffset >= 0 && endOffset <= (m_buffer.size() - 1));

    StopThreads();

    // We are about to modify this range
    GetEditor().Broadcast(std::make_shared<BufferMessage>(this, BufferMessageType::PreBufferChange, startOffset, endOffset));

    auto itrLine = std::lower_bound(m_lineEnds.begin(), m_lineEnds.end(), startOffset);
    if (itrLine == m_lineEnds.end())
    {
        return false;
    }

    auto itrLastLine = std::upper_bound(itrLine, m_lineEnds.end(), endOffset);
    auto offsetDiff = endOffset - startOffset;

    if (*itrLine <= startOffset)
    {
        itrLine++;
    }

    // Adjust all line offsets beyond us
    for (auto itr = itrLastLine; itr != m_lineEnds.end(); itr++)
    {
        *itr -= offsetDiff;
    }

    if (itrLine != itrLastLine)
    {
        m_lineEnds.erase(itrLine, itrLastLine);
        m_processedLine -= long(itrLastLine - itrLine);
    }

    m_buffer.erase(m_buffer.begin() + startOffset, m_buffer.begin() + endOffset);
    assert(m_buffer.size() > 0 && m_buffer[m_buffer.size() - 1] == 0);

    // This is the range we deleted (not valid any more in the buffer)
    GetEditor().Broadcast(std::make_shared<BufferMessage>(this, BufferMessageType::TextDeleted, startOffset, endOffset, cursorAfter));

    return true;
}

BufferLocation PicoVimBuffer::EndLocation() const
{
    auto end = m_buffer.size() - 1;
    return LocationFromOffset(long(end));
}

} // PicoVim namespace

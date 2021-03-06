#pragma once

#include <vector>
#include <string>
#include <map>
#include <set>
#include <deque>
#include <memory>
#include <threadpool/ThreadPool.hpp>
#include <sstream>

// Basic Architecture

// Editor
//      Buffers
//      Modes -> (Active BufferRegion)
// Display
//      BufferRegions (->Buffers)
//
// A buffer is just an array of chars in a gap buffer, with simple operations to insert, delete and search
// A display is something that can display a collection of regions and the editor controls in a window
// A buffer region is a single view onto a buffer inside the main display
//
// The editor has a list of ZepBuffers.
// The editor has different editor modes (vim/standard)
// ZepDisplay can render the editor (with imgui or something else)
// The display has multiple BufferRegions, each a window onto a buffer.
// Multiple regions can refer to the same buffer (N Regions : N Buffers)
// The Modes receive key presses and act on a buffer region
namespace Zep
{

class ZepBuffer;
class ZepMode;
class ZepMode_Vim;
class ZepMode_Standard;
class ZepEditor;
class ZepDisplay;
class ZepSyntax;

// Helper for 2D operations
template<class T>
struct NVec2
{
    NVec2(T xVal, T yVal)
        : x(xVal),
        y(yVal)
    {}

    NVec2()
        : x(0),
        y(0)
    {}

    T x;
    T y;
};
template<class T> inline NVec2<T> operator+ (const NVec2<T>& lhs, const NVec2<T>& rhs) { return NVec2<T>(lhs.x + rhs.x, lhs.y + rhs.y); }
template<class T> inline NVec2<T> operator- (const NVec2<T>& lhs, const NVec2<T>& rhs) { return NVec2<T>(lhs.x - rhs.x, lhs.y - rhs.y); }
template<class T> inline NVec2<T>& operator+= (NVec2<T>& lhs, const NVec2<T>& rhs) { lhs.x += rhs.x; lhs.y += rhs.y; return lhs; }
template<class T> inline NVec2<T>& operator-= (NVec2<T>& lhs, const NVec2<T>& rhs) { lhs.x -= rhs.x; lhs.y -= rhs.y; return lhs; }
template<class T> inline NVec2<T> operator* (const NVec2<T>& lhs, float val) { return NVec2<T>(lhs.x * val, lhs.y * val); }
template<class T> inline NVec2<T>& operator*= (NVec2<T>& lhs, float val) { lhs.x *= val; lhs.y *= val; return lhs; }
template<class T> inline NVec2<T> Clamp(const NVec2<T>& val, const NVec2<T>& min, const NVec2<T>& max)
{
    return NVec2<T>(std::min(max.x, std::max(min.x, val.x)), std::min(max.y, std::max(min.y, val.y)));
}

using NVec2f = NVec2<float>;
using NVec2i = NVec2<long>;

using utf8 = uint8_t;

extern const char* Msg_GetClipBoard;
extern const char* Msg_SetClipBoard;
extern const char* Msg_HandleCommand;

class ZepMessage
{
public:
    ZepMessage(const char* id, const std::string& strIn = std::string())
        : messageId(id),
        str(strIn)
    { }

    const char* messageId;      // Message ID 
    std::string str;            // Generic string for simple messages
    bool handled = false;       // If the message was handled
};

struct IZepClient
{
    virtual void Notify(std::shared_ptr<ZepMessage> message) = 0;
    virtual ZepEditor& GetEditor() const = 0;
};

class ZepComponent : public IZepClient
{
public:
    ZepComponent(ZepEditor& editor);
    virtual ~ZepComponent();
    ZepEditor& GetEditor() const override { return m_editor; }

private:
    ZepEditor& m_editor;
};

// Registers are used by the editor to store/retrieve text fragments
struct Register
{
    Register() : text(""), lineWise(false) {}
    Register(const char* ch, bool lw = false) : text(ch), lineWise(lw) {}
    Register(utf8* ch, bool lw = false) : text((const char*)ch), lineWise(lw) {}
    Register(const std::string& str, bool lw = false) : text(str), lineWise(lw) {}

    std::string text;
    bool lineWise = false;
};

using tRegisters = std::map<std::string, Register>;
using tBuffers = std::deque<std::shared_ptr<ZepBuffer>>;
using tSyntaxFactory = std::function<std::shared_ptr<ZepSyntax>(ZepBuffer*)>;

namespace ZepEditorFlags
{
enum
{
    None = (0),
    DisableThreads = (1 << 0)
};
};

static const char* VimMode = "vim";
static const char* StandardMode = "standard";

class ZepEditor
{
public:

    ZepEditor(uint32_t flags = 0);
    ~ZepEditor();

    ZepMode* GetCurrentMode() const;

    void RegisterMode(const std::string& name, std::shared_ptr<ZepMode> spMode);
    void SetMode(const std::string& mode);
    ZepMode* GetCurrentMode();

    void RegisterSyntaxFactory(const std::string& extension, tSyntaxFactory factory);
    bool Broadcast(std::shared_ptr<ZepMessage> payload);
    void RegisterCallback(IZepClient* pClient) { m_notifyClients.insert(pClient); }
    void UnRegisterCallback(IZepClient* pClient) { m_notifyClients.erase(pClient); }

    const tBuffers& GetBuffers() const;
    ZepBuffer* AddBuffer(const std::string& str);
    ZepBuffer* GetMRUBuffer() const;

    void SetRegister(const std::string& reg, const Register& val);
    void SetRegister(const char reg, const Register& val);
    void SetRegister(const std::string& reg, const char* pszText);
    void SetRegister(const char reg, const char* pszText);
    Register& GetRegister(const std::string& reg);
    Register& GetRegister(const char reg);
    const tRegisters& GetRegisters() const;

    void Notify(std::shared_ptr<ZepMessage> message);
    uint32_t GetFlags() const { return m_flags; }

private:
    std::set<IZepClient*> m_notifyClients;
    mutable tRegisters m_registers;
    
    std::shared_ptr<ZepMode_Vim> m_spVimMode;
    std::shared_ptr<ZepMode_Standard> m_spStandardMode;
    std::map<std::string, tSyntaxFactory> m_mapSyntax;
    std::map<std::string, std::shared_ptr<ZepMode>> m_mapModes;

    // Active mode
    ZepMode* m_pCurrentMode = nullptr;

    // List of buffers that the editor is managing
    // May or may not be visible
    tBuffers m_buffers;
    uint32_t m_flags = 0;
};

} // Zep

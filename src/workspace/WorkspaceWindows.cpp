#include "WorkspaceNode.hpp"

#include <array>
#include <QDir>
#include <QScopeGuard>

#include <string>
#include <windows.h>
#include <winternl.h>

namespace treemcp {
namespace {

QString finalName(HANDLE handle)
{
    const DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (size == 0 || size > 32768) {
        return {};
    }
    std::wstring buffer(size, L'\0');
    const DWORD count = GetFinalPathNameByHandleW(handle, buffer.data(), size, FILE_NAME_NORMALIZED);
    if (count == 0 || count >= size) {
        return {};
    }
    return QString::fromWCharArray(buffer.data(), count);
}

NTSTATUS
openRelative(HANDLE parent, const QString& component, ULONG flags, HANDLE& handle, ULONG sharing = FILE_SHARE_READ)
{
    std::wstring name = component.toStdWString();
    const auto bytes = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    UNICODE_STRING leaf { bytes, bytes, name.data() };
    OBJECT_ATTRIBUTES attributes {};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &leaf;
    IO_STATUS_BLOCK statusBlock {};
    return NtCreateFile(
        &handle,
        FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        &attributes,
        &statusBlock,
        nullptr,
        0,
        sharing,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | flags,
        nullptr,
        0);
}

} // namespace

WorkspaceNode::~WorkspaceNode()
{
    if (native != -1) {
        CloseHandle(reinterpret_cast<HANDLE>(native));
    }
}

WorkspaceNode WorkspaceNode::openRoot(const QString& path)
{
    if (path.isEmpty()) {
        return WorkspaceNode(NodeKind::Error);
    }
    const auto name = QDir::toNativeSeparators(QDir(path).absolutePath()).toStdWString();
    const HANDLE handle = CreateFileW(
        name.c_str(),
        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    BY_HANDLE_FILE_INFORMATION info {};
    if (handle == INVALID_HANDLE_VALUE) {
        return WorkspaceNode(NodeKind::Error);
    }
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        finalName(handle).isEmpty())
    {
        CloseHandle(handle);
        return WorkspaceNode(NodeKind::Error);
    }
    return WorkspaceNode(NodeKind::Directory, reinterpret_cast<qintptr>(handle));
}

WorkspaceNode WorkspaceNode::child(const QString& component) const
{
    if (nodeKind != NodeKind::Directory || !validComponent(component)) {
        return WorkspaceNode(NodeKind::Error);
    }
    HANDLE handle = nullptr;
    const HANDLE directory = reinterpret_cast<HANDLE>(native);
    const NTSTATUS status = openRelative(directory, component, 0, handle);
    if (status < 0) {
        const ULONG error = RtlNtStatusToDosError(status);
        return WorkspaceNode(
            error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? NodeKind::Missing : NodeKind::Error);
    }
    BY_HANDLE_FILE_INFORMATION info {};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info)) {
        CloseHandle(handle);
        return WorkspaceNode(NodeKind::Error);
    }
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        ((info.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) && component != ".gitignore"))
    {
        CloseHandle(handle);
        return WorkspaceNode(NodeKind::Excluded);
    }
    QString parent = finalName(directory);
    if (parent.isEmpty()) {
        CloseHandle(handle);
        return WorkspaceNode(NodeKind::Error);
    }
    if (!parent.endsWith('\\')) {
        parent += '\\';
    }
    if (finalName(handle) != parent + component) {
        CloseHandle(handle);
        return WorkspaceNode(NodeKind::Error);
    }
    return WorkspaceNode(
        info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? NodeKind::Directory : NodeKind::File,
        reinterpret_cast<qintptr>(handle));
}

bool WorkspaceNode::list(const EntryVisitor& visit) const
{
    // Independent open file descriptions keep concurrent walks from sharing a cursor.
    HANDLE directory = nullptr;
    if (openRelative(
            reinterpret_cast<HANDLE>(native),
            {},
            FILE_DIRECTORY_FILE,
            directory,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE) < 0)
    {
        return false;
    }
    const auto guard = qScopeGuard([directory]() { CloseHandle(directory); });
    FILE_INFO_BY_HANDLE_CLASS operation = FileIdBothDirectoryRestartInfo;
    alignas(FILE_ID_BOTH_DIR_INFO) std::array<std::byte, 8192> buffer;
    while (GetFileInformationByHandleEx(directory, operation, buffer.data(), sizeof(buffer))) {
        operation = FileIdBothDirectoryInfo;
        size_t offset = 0;
        for (;;) {
            const void* address = buffer.data() + offset;
            const auto* entry = static_cast<const FILE_ID_BOTH_DIR_INFO*>(address);
            const auto length = static_cast<qsizetype>(entry->FileNameLength / sizeof(WCHAR));
            const QString name = QStringView(entry->FileName, length).toString();
            const auto attributes = entry->FileAttributes;
            const NodeKind kind = (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE))
                ? NodeKind::Excluded
                : (attributes & FILE_ATTRIBUTE_DIRECTORY) ? NodeKind::Directory
                                                          : NodeKind::File;
            if (!visit(name, kind)) {
                return true;
            }
            if (!entry->NextEntryOffset) {
                break;
            }
            offset += entry->NextEntryOffset;
        }
    }
    return GetLastError() == ERROR_NO_MORE_FILES;
}

std::optional<FileStamp> WorkspaceNode::stamp() const
{
    const HANDLE handle = reinterpret_cast<HANDLE>(native);
    BY_HANDLE_FILE_INFORMATION info {};
    FILE_BASIC_INFO times {};
    if (!GetFileInformationByHandle(handle, &info) ||
        !GetFileInformationByHandleEx(handle, FileBasicInfo, &times, sizeof(times)))
    {
        return std::nullopt;
    }
    const quint64 id = (quint64 { info.nFileIndexHigh } << 32) | info.nFileIndexLow;
    const quint64 bytes = (quint64 { info.nFileSizeHigh } << 32) | info.nFileSizeLow;
    return FileStamp { info.dwVolumeSerialNumber,
                       id,
                       static_cast<qint64>(bytes),
                       times.LastWriteTime.QuadPart,
                       times.ChangeTime.QuadPart,
                       0,
                       0 };
}

qsizetype WorkspaceNode::read(char* buffer, qsizetype size) const
{
    DWORD count = 0;
    return ReadFile(reinterpret_cast<HANDLE>(native), buffer, static_cast<DWORD>(size), &count, nullptr)
        ? static_cast<qsizetype>(count)
        : -1;
}

} // namespace treemcp

using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace Hitori.Build
{
    // PublishSymbols.ps1이 Add-Type으로 로드하는 Windows 네이티브 함수 연동용 형식
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct SymbolFileInfo
    {
        public uint StructSize;
        // 조회한 EXE/PDB 파일명
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 261)] public string File;
        // 디버그 정보 분리 여부
        [MarshalAs(UnmanagedType.Bool)] public bool Stripped;

        public uint Timestamp;
        public uint ImageSize;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 261)] public string DbgFile;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 261)] public string PdbFile;

        public Guid Guid;
        // 이전 형식의 PDB 식별값(필드 위치 조정)
        public uint Signature;
        public uint Age;
    }

    public static class Symbols
    {
        // Windows가 제공하는 dbghelp.dll에서 유니코드 경로용 심볼 식별정보 조회 함수를 가져옴
        [DllImport("dbghelp.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SymSrvGetFileIndexInfoW(string file, ref SymbolFileInfo info, uint flags);

        public static SymbolFileInfo Read(string path)
        {
            // DLL이 전달받은 구조체 형식을 확인
            var info = new SymbolFileInfo();
            info.StructSize = (uint)Marshal.SizeOf(typeof(SymbolFileInfo));

            // 파일을 읽어 GUID·Age 및 EXE 이미지 정보를 구조체에 채움
            if (!SymSrvGetFileIndexInfoW(path, ref info, 0))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot read symbol identity: " + path);

            // 호출자는 EXE와 PDB에서 각각 읽은 구조체의 식별값을 비교
            return info;
        }
    }
}

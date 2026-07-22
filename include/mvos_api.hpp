// ============================================================================
//  mvos_api.hpp  —  GENERATED signature reference for libmvos.so (Theocracy)
//  Regenerate:  python3 tools/gen_headers.py > include/mvos_api.hpp
//  Source:      data/mvos_api.json (see docs/reference/mvos-api-inventory.md)
//
//  WHAT THIS IS: the demangled method contract for every engine class, each
//  method tagged with its file address (@0x..., = Ghidra addr - 0x10000). Use it
//  as the HLE implementation worklist and signature map.
//
//  WHAT THIS IS NOT (yet): a standalone translation unit. Known gaps, all
//  inherent to the symbol table (fill in as you implement, from Ghidra):
//   * mvret /*ret?*/  = return type unknown (GNU-v2 mangling omits it).
//   * [polymorphic]   = has a vtable, but WHICH methods are virtual and their
//                       slot ORDER are not in the symbols — read Ghidra vtables.
//   * bases are vtable-mixin hints only (primary base + order not recoverable).
//   * template params (tPoint<long>, cArray<...>) and enums (eBMType, ...) are
//     referenced but not defined here; real defs come with the layout work.
//   * field layouts appear only for hand-reversed classes (see docs/structs).
// ============================================================================
#ifndef MVOS_API_HPP
#define MVOS_API_HPP

typedef int mvret;   // placeholder for unknown return types (grep: /*ret?*/)

// ---- forward declarations ----
class CHARCONVERT;
class CHARFILTER;
class DEC;
class DOUBLE;
class HEX;
class LastChance_SPR0;
class LastChance_TER0;
class Log;
class Log_File;
class Log_SafeFile;
class Log_Stdout;
class [thunk 112] cVOEditRow;
class [thunk 12] cData_AnimBitmap;
class [thunk 12] cData_Bitmap;
class [thunk 12] cData_Font;
class [thunk 12] cData_Palette;
class [thunk 12] cData_Sample;
class [thunk 12] cIPCSession_IPX;
class [thunk 12] cPalette;
class [thunk 12] cSoundChannel_SoftwareMix;
class [thunk 24] cAnimSkeleton;
class [thunk 24] cFLCAnimPlayer;
class [thunk 36] cAnimSkeleton;
class [thunk 4] cSoundCard_Linux;
class [thunk 4] cSoundCard_SoftwareMix;
class [thunk 56] cAnimSkeleton;
class [thunk 56] cFLCAnimPlayer;
class cAO;
class cAnimBitmap;
class cAnimSkeleton;
class cAnimTextReader;
class cApplication;
class cBitmap;
class cCD_Linux;
class cColor;
class cCompatibilityFileSystem;
class cConsole;
class cConsoleVO;
class cConvert;
class cData_AnimBitmap;
class cData_Bitmap;
class cData_Font;
class cData_MapItem;
class cData_Palette;
class cData_Sample;
class cDayTime;
class cDimension;
class cDirectory;
class cDirent;
class cDirentname;
class cEditRow;
class cEnvClass;
class cEnvSystem;
class cEnvVar;
class cEvent;
class cEventManager;
class cEventManager_Linux;
class cEventMulti;
class cEvent_Forwarder;
class cFFHidden;
class cFFilter;
class cFLCAnimPlayer;
class cFile;
class cFileSystem;
class cFont;
class cGD;
class cGD_LFB15;
class cGD_LFB16;
class cGD_LFB24;
class cGD_LFB32;
class cGD_LFB8;
class cGD_Minimal;
class cGD_SFB16;
class cGD_SFB8;
class cHList;
class cHNode;
class cHeap;
class cHeapBlock;
class cHeap_Compatibility;
class cIPCBrowser;
class cIPCBrowser_IPX;
class cIPCBrowser_TCPIP;
class cIPCO;
class cIPCOStream2Block;
class cIPCO_IPX;
class cIPCO_TCPIP;
class cIPCServer;
class cIPCServer_IPX;
class cIPCServer_IPX::cClientInfo;
class cIPCServer_TCPIP;
class cIPCSession;
class cIPCSession_IPX;
class cIPCSession_TCPIP;
class cIPCSystem;
class cIPCSystem_Linux;
class cIPXPacketHeader;
class cIPXSocket;
class cIPXSocket_Linux;
class cIntuition;
class cKeySequence;
class cKeyboard;
class cLibKeyboard;
class cLibMouse;
class cLibPointer;
class cLibVVC;
class cLibrary;
class cLineEditor;
class cLinuxTimer;
class cList;
class cLocaleDataBase;
class cLocaleEntry;
class cMasterVO;
class cMemBlock;
class cMemBlockPTR;
class cMemBlock_;
class cMemoryPipe;
class cMixer;
class cMouse;
class cMsgCenter;
class cMsgRecNode;
class cMsgReceiver;
class cMsgSender;
class cMsgTypeNode;
class cNode;
class cOldString;
class cPalette;
class cPalette15;
class cPalette16;
class cPalette32;
class cPaletteFull;
class cPipe;
class cPointer;
class cProcIniFile;
class cProcess;
class cRandom;
class cRectangle;
class cSample;
class cScreen;
class cSemaphore;
class cSharedData_AnimBitmap;
class cSharedData_Bitmap;
class cShell;
class cSoundCard;
class cSoundCard_Dummy;
class cSoundCard_Linux;
class cSoundCard_SoftwareMix;
class cSoundChannel;
class cSoundChannel_SoftwareMix;
class cSoundConvert;
class cSoundFormat;
class cSoundNotify;
class cSoundPlay;
class cSoundRecorderBuffer;
class cSoundRecorderThread;
class cSoundServer;
class cSoundServerChannel;
class cSprABitmapAdd;
class cSprClick;
class cSprite;
class cStdConv;
class cStream;
class cString;
class cSyncSystem;
class cSystemMemory;
class cTask;
class cTask_;
class cTextFile;
class cThread;
class cThread_;
class cTimerSystem;
class cTimerSystem_Linux;
class cVCD;
class cVMode;
class cVModeRequest;
class cVOAButton;
class cVOBitmap;
class cVOButton;
class cVOConsole;
class cVODragBox;
class cVOEditRow;
class cVOFGraphs;
class cVOFiler;
class cVOListReq;
class cVOMsgBox;
class cVOMultiLine;
class cVOPulldown;
class cVOSliderV;
class cVOTextBox;
class cVOWindow;
class cVObject;
class cVTimer;
class cVVC;
class cVolume;
class cVolumeHP;
struct sBMPHeader::sBMPInfoHeader;
struct sFLC_Chunk;
struct sFLC_Frame;
struct sFLC_FrameBlack;
struct sFLC_FrameByteRun;
struct sFLC_FrameColor;
struct sFLC_FrameColor256;
struct sFLC_FrameLC;
struct sFLC_FrameSS2;
struct sFLC_FrameSound;
struct sFLC_FrameUncompressed;
struct sFLC_Header;
struct sFLC_Prefix;
struct sFLC_SubChunk;
struct sInput;
struct sMCoord;
struct sMVOSANIMHeader;
struct sRawPicHeader;
struct sRiff;
struct sSPR1;
struct sTER1;
struct sVModeInfo;
struct sVOMessage;
struct sWave;
struct sWave::sData;
struct sWave::sFormat;

// ==================== CLASSES (220) ====================

// ==== CHARCONVERT ==============================================
class CHARCONVERT {
public:
    CHARCONVERT(const char *, const char *);   // @00084cf0
    mvret operator[](unsigned char);   // @00084ce0  /*ret?*/
};

// ==== CHARFILTER ===============================================
class CHARFILTER {
public:
    CHARFILTER(const char *);   // @00084db0
    CHARFILTER(CHARFILTER &, const char *);   // @00084de0
    mvret operator[](unsigned char);   // @00084e20  /*ret?*/
    mvret Add(const char *);   // @00084d80  /*ret?*/
    mvret Sub(const char *);   // @00084d50  /*ret?*/
};

// ==== DEC ======================================================
class DEC {
public:
    DEC(long);   // @00092290
    operator const char *();   // @00092280
};

// ==== DOUBLE ===================================================
class DOUBLE {
public:
    mvret operator%(DOUBLE);   // @00090a00  /*ret?*/
    mvret operator%=(DOUBLE);   // @00090a30  /*ret?*/
    mvret Round();   // @00090ec0  /*ret?*/
    mvret Floor();   // @00090e80  /*ret?*/
    mvret ACos();   // @00090a60  /*ret?*/
    mvret Power(DOUBLE);   // @00090c10  /*ret?*/
    mvret Sinh();   // @00090d00  /*ret?*/
    mvret ATan2(DOUBLE);   // @00090af0  /*ret?*/
    mvret Tan();   // @00090d80  /*ret?*/
    mvret Ceil();   // @00090e40  /*ret?*/
    mvret Cos();   // @00090b10  /*ret?*/
    mvret Exp();   // @00090b80  /*ret?*/
    mvret Tanh();   // @00090da0  /*ret?*/
    mvret Log();   // @00090bd0  /*ret?*/
    mvret Log10();   // @00090bf0  /*ret?*/
    mvret ASin();   // @00090aa0  /*ret?*/
    mvret Abs();   // @00090bb0  /*ret?*/
    mvret ATan();   // @00090ad0  /*ret?*/
    mvret Sin();   // @00090ce0  /*ret?*/
    mvret Sqrt();   // @00090e20  /*ret?*/
    mvret Cosh();   // @00090b30  /*ret?*/
};

// ==== HEX ======================================================
class HEX {
public:
    HEX(unsigned long);   // @000922c0
    operator const char *();   // @000922b0
};

// ==== LastChance_SPR0 ==========================================
class LastChance_SPR0 {
public:
    mvret MakeAnimBitmap(cAnimBitmap &, unsigned long);   // @0004d9d0  /*ret?*/
    mvret Filter();   // @00096980  /*ret?*/
};

// ==== LastChance_TER0 ==========================================
class LastChance_TER0 {
public:
    mvret MakeAnimBitmap(cAnimBitmap &, unsigned long);   // @0004e020  /*ret?*/
};

// ==== Log [polymorphic, rtti] ==================================
class Log {
public:
};

// ==== Log_File [polymorphic, rtti] =============================
class Log_File {
public:
    Log_File(const char *);   // @000921c0
    ~Log_File();   // @00092180
    mvret __ls(const char *) const;   // @00091ff0  /*ret?*/
};

// ==== Log_SafeFile [polymorphic, rtti] =========================
class Log_SafeFile {
public:
    Log_SafeFile(const char *);   // @00092120
    mvret __ls(const char *) const;   // @00092020  /*ret?*/
};

// ==== Log_Stdout [polymorphic, rtti] ===========================
class Log_Stdout {
public:
    mvret __ls(const char *) const;   // @00091fd0  /*ret?*/
};

// ==== [thunk 112] cVOEditRow ===================================
class [thunk 112] cVOEditRow {
public:
    mvret Deletion();   // @00096ae0  /*ret?*/
    mvret Empty();   // @00096ad0  /*ret?*/
};

// ==== [thunk 12] cData_AnimBitmap ==============================
class [thunk 12] cData_AnimBitmap {
public:
    mvret _Validate();   // @00096890  /*ret?*/
};

// ==== [thunk 12] cData_Bitmap ==================================
class [thunk 12] cData_Bitmap {
public:
    mvret _Validate();   // @000968a0  /*ret?*/
};

// ==== [thunk 12] cData_Font ====================================
class [thunk 12] cData_Font {
public:
    mvret _Validate();   // @000970f0  /*ret?*/
};

// ==== [thunk 12] cData_Palette =================================
class [thunk 12] cData_Palette {
public:
    mvret _Validate();   // @000968b0  /*ret?*/
};

// ==== [thunk 12] cData_Sample ==================================
class [thunk 12] cData_Sample {
public:
    mvret _Validate();   // @00096ac0  /*ret?*/
};

// ==== [thunk 12] cIPCSession_IPX ===============================
class [thunk 12] cIPCSession_IPX {
public:
    ~cIPCSession_IPX();   // @00097270
    mvret Open();   // @00097280  /*ret?*/
};

// ==== [thunk 12] cPalette ======================================
class [thunk 12] cPalette {
public:
    mvret _Validate();   // @000983a0  /*ret?*/
};

// ==== [thunk 12] cSoundChannel_SoftwareMix =====================
class [thunk 12] cSoundChannel_SoftwareMix {
public:
    ~cSoundChannel_SoftwareMix();   // @000984d0
    mvret IsStereo();   // @00098500  /*ret?*/
    mvret Play_(cSample &, unsigned long, unsigned long);   // @000984e0  /*ret?*/
    mvret SetVolume(const cVolume &);   // @00098520  /*ret?*/
    mvret ExitLoop();   // @00098540  /*ret?*/
    mvret Stop();   // @000984f0  /*ret?*/
    mvret SetSpeed(unsigned short);   // @00098530  /*ret?*/
    mvret GetFormat();   // @00098510  /*ret?*/
};

// ==== [thunk 24] cAnimSkeleton =================================
class [thunk 24] cAnimSkeleton {
public:
    ~cAnimSkeleton();   // @000966e0
};

// ==== [thunk 24] cFLCAnimPlayer ================================
class [thunk 24] cFLCAnimPlayer {
public:
    ~cFLCAnimPlayer();   // @000970e0
};

// ==== [thunk 36] cAnimSkeleton =================================
class [thunk 36] cAnimSkeleton {
public:
    mvret _Validate();   // @000966d0  /*ret?*/
};

// ==== [thunk 4] cSoundCard_Linux ===============================
class [thunk 4] cSoundCard_Linux {
public:
    ~cSoundCard_Linux();   // @00098ab0
    mvret Main();   // @00098aa0  /*ret?*/
};

// ==== [thunk 4] cSoundCard_SoftwareMix =========================
class [thunk 4] cSoundCard_SoftwareMix {
public:
    ~cSoundCard_SoftwareMix();   // @00098420
};

// ==== [thunk 56] cAnimSkeleton =================================
class [thunk 56] cAnimSkeleton {
public:
    ~cAnimSkeleton();   // @000966c0
};

// ==== [thunk 56] cFLCAnimPlayer ================================
class [thunk 56] cFLCAnimPlayer {
public:
    ~cFLCAnimPlayer();   // @000970c0
    mvret TimerProc();   // @000970d0  /*ret?*/
};

// ==== cAO ======================================================
class cAO {
public:
    cAO(cSample *, cVolume, short);   // @00082f50
    ~cAO();   // @00082f20
    mvret GetPriority();   // @00082f00  /*ret?*/
    mvret GetVolume();   // @00082e60  /*ret?*/
    mvret SetVolume(const cVolume &);   // @00082840  /*ret?*/
    mvret IsActive();   // @00082e80  /*ret?*/
    mvret Stop();   // @00082820  /*ret?*/
    mvret SetPriority(unsigned short);   // @00082ee0  /*ret?*/
    mvret GetImportance();   // @00082eb0  /*ret?*/
    mvret GetSample();   // @00082ea0  /*ret?*/
};

// ==== cAnimBitmap [polymorphic, rtti] ==========================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cAnimBitmap {
public:
    cAnimBitmap();   // @0004ae50
    cAnimBitmap(unsigned long, unsigned long, eBMType);   // @0004af20
    ~cAnimBitmap();   // @0004b0e0
    mvret SetFrame(unsigned long, unsigned long, const cRectangle &);   // @0004b7a0  /*ret?*/
    mvret GetRectAddress() const;   // @0004b7f0  /*ret?*/
    mvret GetBoundingBox();   // @0004b380  /*ret?*/
    mvret SetType(eBMType);   // @0004b790  /*ret?*/
    mvret GetAddress(unsigned long);   // @0004b6b0  /*ret?*/
    mvret GetBaseAddress();   // @0004b6f0  /*ret?*/
    mvret SetCP(unsigned long, const tPoint<long> &);   // @0004b260  /*ret?*/
    mvret Construct(unsigned long, unsigned long, eBMType);   // @0004b280  /*ret?*/
    mvret GetPalette(unsigned long);   // @0004b4f0  /*ret?*/
    mvret GetRectangle(unsigned long);   // @0004b5e0  /*ret?*/
    mvret GetNumberOfFrames();   // @0004b580  /*ret?*/
    mvret GetType();   // @0004b730  /*ret?*/
    mvret GetSize();   // @0004b690  /*ret?*/
    mvret GetNumOfPalette();   // @0004b570  /*ret?*/
    mvret GetSize(unsigned long);   // @0004b650  /*ret?*/
    mvret GetOffsetAddress() const;   // @0004b7e0  /*ret?*/
};

// ==== cAnimSkeleton [polymorphic, rtti] ========================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): tMemBlock<unsigned char>, cVTimer, cMemBlock_
class cAnimSkeleton {
public:
    cAnimSkeleton(const char *, unsigned long);   // @0004a2d0
    ~cAnimSkeleton();   // @0004a410
    mvret Alloc(unsigned long);   // @0004a720  /*ret?*/
    mvret Info();   // @0004a5b0  /*ret?*/
    mvret ReadAhead();   // @0004a600  /*ret?*/
    mvret GetFreeSize();   // @0004a6d0  /*ret?*/
    mvret Free(unsigned long);   // @0004a790  /*ret?*/
    mvret Play(bool);   // @0004a500  /*ret?*/
    mvret _Validate();   // @0004a7a0  /*ret?*/
};

// ==== cAnimTextReader ==========================================
class cAnimTextReader {
public:
    cAnimTextReader(char *, unsigned long);   // @0004b8f0
    mvret Process_Text(char *, unsigned long);   // @0004b930  /*ret?*/
    mvret GetText();   // @0004b840  /*ret?*/
    mvret Process_Text_ObjectBuilder(char *, unsigned long);   // @0004ba00  /*ret?*/
    mvret Process_Text_Preprocess(char *, unsigned long);   // @0004b9b0  /*ret?*/
};

// ==== cApplication [polymorphic, rtti] =========================
class cApplication {
public:
    mvret IsNetworkRequired();   // @000479f0  /*ret?*/
    mvret RedbookRequired();   // @00047ab0  /*ret?*/
    mvret IsTimerRequired();   // @00047a00  /*ret?*/
    mvret VideoRequired();   // @00047af0  /*ret?*/
    mvret IsPointerRequired();   // @00047a30  /*ret?*/
    mvret MouseRequired();   // @00047ae0  /*ret?*/
    mvret IsRedbookRequired();   // @00047a10  /*ret?*/
    mvret KeyboardRequired();   // @00047ac0  /*ret?*/
    mvret IsVideoRequired();   // @00047a50  /*ret?*/
    mvret IntuitionRequired();   // @00047a70  /*ret?*/
    mvret IsSoundRequired();   // @00047a60  /*ret?*/
    mvret TimerRequired();   // @00047aa0  /*ret?*/
    mvret IsMouseRequired();   // @00047a40  /*ret?*/
    mvret PointerRequired();   // @00047ad0  /*ret?*/
    mvret NetworkRequired();   // @00047a90  /*ret?*/
    mvret IsKeyboardRequired();   // @00047a20  /*ret?*/
    mvret IsIntuitionRequired();   // @000479e0  /*ret?*/
    mvret SoundRequired();   // @00047b00  /*ret?*/
};

// ==== cBitmap [polymorphic, rtti] ==============================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cBitmap {
public:
    cBitmap(cBitmap *);   // @0004a990
    cBitmap(const cBitmap &);   // @0004ab90
    cBitmap(const cDimension &, eBMType);   // @0004a7f0
    ~cBitmap();   // @000962f0
    mvret Construct(const cDimension &, eBMType);   // @0004adf0  /*ret?*/
    mvret CreateGD();   // @00085800  /*ret?*/
};

// ==== cCD_Linux [polymorphic, rtti] ============================
class cCD_Linux {
public:
    cCD_Linux(const char *);   // @00091f80
    ~cCD_Linux();   // @00091f40
    mvret GetVolume();   // @00091e70  /*ret?*/
    mvret Stop();   // @00091d20  /*ret?*/
    mvret GetActualTrack();   // @00091de0  /*ret?*/
    mvret Resume();   // @00091cd0  /*ret?*/
    mvret Pause();   // @00091c80  /*ret?*/
    mvret Play_Real(unsigned long, unsigned long);   // @00091c10  /*ret?*/
    mvret GetCDInfo();   // @00091d70  /*ret?*/
    mvret SetVolume(unsigned short);   // @00091ec0  /*ret?*/
};

// ==== cColor ===================================================
class cColor {
public:
    cColor();   // @000802d0
    cColor(unsigned char, unsigned char, unsigned char);   // @000802a0
    mvret GetR() const;   // @00080160  /*ret?*/
    mvret ChangeIntensity(float);   // @0007ff30  /*ret?*/
    mvret GetIntensity(float) const;   // @0007fdd0  /*ret?*/
    mvret GetColor32() const;   // @00080070  /*ret?*/
    mvret Brightness(unsigned long);   // @00080260  /*ret?*/
    mvret GetG() const;   // @00080140  /*ret?*/
    mvret GetB() const;   // @00080120  /*ret?*/
    mvret Red(unsigned long);   // @00080240  /*ret?*/
    mvret Blue(unsigned long);   // @00080200  /*ret?*/
    mvret ShiftRGB(unsigned long, unsigned long, unsigned long);   // @000801c0  /*ret?*/
    mvret Green(unsigned long);   // @00080220  /*ret?*/
    mvret GetColor15() const;   // @000800e0  /*ret?*/
    mvret __mi(cColor) const;   // @00080180  /*ret?*/
    mvret GetColor16() const;   // @000800a0  /*ret?*/
};

// ==== cCompatibilityFileSystem [polymorphic, rtti] =============
class cCompatibilityFileSystem {
public:
    mvret Close();   // @000532d0  /*ret?*/
    mvret Open(const char *, eFileOpeningMode, bool);   // @00053270  /*ret?*/
    mvret GetPosition();   // @000532f0  /*ret?*/
    mvret Read(PTR, unsigned long);   // @00053380  /*ret?*/
    mvret Rename(const char *, const char *);   // @00053410  /*ret?*/
    mvret IsEnd();   // @000533c0  /*ret?*/
    mvret GetFileMode();   // @00053260  /*ret?*/
    mvret Write(PTR, unsigned long);   // @000533a0  /*ret?*/
    mvret Seek(long, eFileSeekWhence);   // @00053310  /*ret?*/
    mvret Delete(const char *);   // @000533f0  /*ret?*/
};

// ==== cConsole [polymorphic, rtti] =============================
class cConsole {
public:
    cConsole(int);   // @00042a60
    ~cConsole();   // @00042b30
    mvret ChangeShell(cShell *);   // @00042bc0  /*ret?*/
    mvret Process(const char *);   // @00042e30  /*ret?*/
    mvret IncraseLine(int, int);   // @00042e70  /*ret?*/
    mvret Print(const char *, ...);   // @00042c40  /*ret?*/
    mvret GetLastLine();   // @00042fa0  /*ret?*/
    mvret GetPreviousInput();   // @00042fe0  /*ret?*/
    mvret GetLines(int, int);   // @00042ee0  /*ret?*/
    mvret HaveShell();   // @000430d0  /*ret?*/
    mvret GetNextInput();   // @00043020  /*ret?*/
    mvret IsModified();   // @000430c0  /*ret?*/
    mvret GetShell();   // @000430f0  /*ret?*/
    mvret Input(const char *, ...);   // @00042d70  /*ret?*/
    mvret DisableLineWrap(int);   // @00043070  /*ret?*/
    mvret Clear();   // @00042ec0  /*ret?*/
    mvret GetBufferSize();   // @000430b0  /*ret?*/
    mvret RestoreShell();   // @00042c00  /*ret?*/
    mvret EnableLineWrap(int);   // @00043090  /*ret?*/
    mvret DecraseLine(int, int);   // @00042e90  /*ret?*/
};

// ==== cConsoleVO [polymorphic, rtti] ===========================
class cConsoleVO {
public:
    cConsoleVO(const cRectangle &, cColor, cData_Font &, cVOConsole *, bool);   // @00043d20
    ~cConsoleVO();   // @00044480
    mvret Key(eKeyCode, int);   // @00044c70  /*ret?*/
    mvret SetExitKey(eKeyCode, unsigned char);   // @00044eb0  /*ret?*/
    mvret Process(sInput &);   // @00044be0  /*ret?*/
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @00044550  /*ret?*/
};

// ==== cConvert =================================================
class cConvert {
public:
    cConvert(const cPalette &, const cPalette &, unsigned long);   // @0007f960
    mvret operator[](unsigned long);   // @0007f940  /*ret?*/
    operator unsigned char *();   // @0007f930
};

// ==== cData_AnimBitmap [polymorphic, rtti] =====================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cData_AnimBitmap {
public:
    cData_AnimBitmap(const char *, bool);   // @0004e240
    cData_AnimBitmap();   // @0004fbb0
    ~cData_AnimBitmap();   // @0004fb00
    mvret Const(char *);   // @0004faf0  /*ret?*/
    mvret LoadMP(unsigned char *, unsigned long);   // @0004eac0  /*ret?*/
    mvret _Validate();   // @0004fbe0  /*ret?*/
    mvret SaveMP(const char *);   // @0004f060  /*ret?*/
    mvret GetFilename();   // @0004fae0  /*ret?*/
    mvret Load();   // @0004e280  /*ret?*/
};

// ==== cData_Bitmap [polymorphic, rtti] =========================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cData_Bitmap {
public:
    cData_Bitmap(const char *, bool);   // @0004cd80
    ~cData_Bitmap();   // @0004fc70
    mvret Load();   // @0004d070  /*ret?*/
    mvret _Validate();   // @0004fd70  /*ret?*/
    mvret GetFilename();   // @0004fc60  /*ret?*/
};

// ==== cData_Font [polymorphic, rtti] ===========================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cData_Font {
public:
    cData_Font(const char *, bool);   // @000575a0
    ~cData_Font();   // @000575f0
    mvret Load();   // @00057640  /*ret?*/
    mvret _Validate();   // @00059030  /*ret?*/
    mvret MakeMultiPal(void *);   // @00057b40  /*ret?*/
};

// ==== cData_MapItem [polymorphic, rtti] ========================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cData_MapItem {
public:
    cData_MapItem(const char *);   // @0004f970
    cData_MapItem();   // @0004f9a0
    ~cData_MapItem();   // @0004fa20
};

// ==== cData_Palette [polymorphic, rtti] ========================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cData_Palette {
public:
    cData_Palette(const char *);   // @0004d770
    ~cData_Palette();   // @0004fe70
    mvret Load();   // @0004d810  /*ret?*/
    mvret _Validate();   // @0004fdf0  /*ret?*/
};

// ==== cData_Sample [polymorphic, rtti] =========================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cData_Sample {
public:
    cData_Sample(const char *);   // @00050640
    ~cData_Sample();   // @00050870
    mvret Load();   // @000506d0  /*ret?*/
    mvret _Validate();   // @000507f0  /*ret?*/
};

// ==== cDayTime =================================================
class cDayTime {
public:
    mvret operator*(double);   // @000956c0  /*ret?*/
    mvret operator-=(cDayTime);   // @000954e0  /*ret?*/
    mvret operator+(cDayTime);   // @000954a0  /*ret?*/
    mvret operator-(cDayTime);   // @00095450  /*ret?*/
    mvret operator+=(cDayTime);   // @00095520  /*ret?*/
    mvret operator/=(double);   // @00095560  /*ret?*/
    mvret operator/(double);   // @00095640  /*ret?*/
    mvret SetBySys();   // @00095420  /*ret?*/
    mvret Set(long, long);   // @000953c0  /*ret?*/
    mvret __aml(double);   // @000955d0  /*ret?*/
};

// ==== cDimension [rtti] ========================================
class cDimension {
public:
    mvret Set(unsigned long, unsigned long);   // @000964a0  /*ret?*/
};

// ==== cDirectory [polymorphic, rtti] ===========================
class cDirectory {
public:
    ~cDirectory();   // @00097050
    mvret ReOpen(cDirent &);   // @0004bde0  /*ret?*/
    mvret Refresh();   // @0004bbd0  /*ret?*/
    mvret ReOpen(const char *);   // @0004bc20  /*ret?*/
    mvret Open();   // @0004bab0  /*ret?*/
};

// ==== cDirent [polymorphic, rtti] ==============================
class cDirent {
public:
    cDirent(const cDirentname &);   // @0004bf30
    cDirent(const char *);   // @0004c030
    ~cDirent();   // @0004c2e0
    mvret GetType();   // @0004c250  /*ret?*/
    mvret Cmp(cDirent &);   // @0004c200  /*ret?*/
    mvret Validate();   // @0004c120  /*ret?*/
    mvret GetBasename();   // @0004c270  /*ret?*/
    mvret IsValid();   // @0004c230  /*ret?*/
    mvret GetSize();   // @0004c260  /*ret?*/
    mvret GetFilename();   // @0004c2a0  /*ret?*/
};

// ==== cDirentname ==============================================
class cDirentname {
public:
    mvret operator+=(const char *);   // @00092640  /*ret?*/
    mvret IsRootDir(const char *);   // @00092960  /*ret?*/
    mvret CheckExtension(const char *);   // @00092890  /*ret?*/
    mvret IsAbsolute();   // @00092940  /*ret?*/
    mvret IsHidden();   // @000928f0  /*ret?*/
};

// ==== cEditRow [polymorphic, rtti] =============================
class cEditRow {
public:
    cEditRow(unsigned char);   // @000528d0
    ~cEditRow();   // @00052890
    operator char *();   // @00052880
    mvret operator=(const char *);   // @000509d0  /*ret?*/
    mvret InputChFilter(char);   // @00052860  /*ret?*/
    mvret Action(eER_Action, char);   // @00050900  /*ret?*/
    mvret Insert(char);   // @00050a50  /*ret?*/
    mvret Empty();   // @000509b0  /*ret?*/
    mvret Replace(char);   // @00050ab0  /*ret?*/
    mvret Deletion();   // @00050af0  /*ret?*/
};

// ==== cEnvClass [polymorphic, rtti] ============================
class cEnvClass {
public:
    cEnvClass(const char *);   // @00052fe0
    ~cEnvClass();   // @00052f60
    mvret GetName();   // @00052f50  /*ret?*/
    mvret FindVariable(const char *);   // @00052c40  /*ret?*/
    mvret CreateVariable(const char *, const char *);   // @00052bb0  /*ret?*/
};

// ==== cEnvSystem ===============================================
class cEnvSystem {
public:
    cEnvSystem();   // @00052f30
    ~cEnvSystem();   // @00052ee0
    mvret FindClass(const char *);   // @00052d80  /*ret?*/
    mvret ProcIniFile(const char *, unsigned long);   // @00052dd0  /*ret?*/
    mvret CreateClass(const char *);   // @00052ca0  /*ret?*/
};

// ==== cEnvVar [polymorphic, rtti] ==============================
class cEnvVar {
public:
    cEnvVar(const char *, const char *);   // @00052940
    ~cEnvVar();   // @00053140
    operator cOldString();   // @000530a0
    mvret HaveMoreElement();   // @000530c0  /*ret?*/
    mvret Reset();   // @000530d0  /*ret?*/
    mvret GetName();   // @00053100  /*ret?*/
    mvret GetValue(int);   // @00052b30  /*ret?*/
    mvret GetCount();   // @000530f0  /*ret?*/
};

// ==== cEvent [polymorphic, rtti] ===============================
class cEvent {
public:
};

// ==== cEventManager [rtti] =====================================
class cEventManager {
public:
};

// ==== cEventManager_Linux [polymorphic, rtti] ==================
class cEventManager_Linux {
public:
    cEventManager_Linux();   // @000959e0
    mvret Add(void *);   // @00095a30  /*ret?*/
    mvret Clear();   // @00095a00  /*ret?*/
    mvret IsSignaled(void *);   // @00095a80  /*ret?*/
    mvret WaitForEvent(cTime_US *);   // @00095ab0  /*ret?*/
    mvret Remove(void *);   // @00095a60  /*ret?*/
};

// ==== cEventMulti [polymorphic, rtti] ==========================
class cEventMulti {
public:
};

// ==== cEvent_Forwarder [polymorphic, rtti] =====================
class cEvent_Forwarder {
public:
    cEvent_Forwarder(cEventMulti *);   // @000531b0
    mvret Signal();   // @00053190  /*ret?*/
};

// ==== cFFHidden [polymorphic, rtti] ============================
class cFFHidden {
public:
    cFFHidden();   // @00096e70
    ~cFFHidden();   // @00096fd0
    mvret Check(cDirentname &);   // @00096f90  /*ret?*/
};

// ==== cFFilter [polymorphic, rtti] =============================
class cFFilter {
public:
    ~cFFilter();   // @00096eb0
};

// ==== cFLCAnimPlayer [polymorphic, rtti] =======================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_, tMemBlock<unsigned char>, cVTimer
class cFLCAnimPlayer {
public:
    cFLCAnimPlayer(const char *, unsigned long);   // @000566d0
    ~cFLCAnimPlayer();   // @00056600
    mvret TimerProc();   // @00055bc0  /*ret?*/
    mvret Preprocess();   // @000559c0  /*ret?*/
    mvret PlayFrame(cGD &, const cRectangle &);   // @00055bb0  /*ret?*/
};

// ==== cFile [polymorphic, rtti] ================================
class cFile {
public:
    cFile(const char *);   // @000553d0
    cFile(const cFile &);   // @00055420
    ~cFile();   // @00055380
    mvret Read(void *, unsigned long);   // @00054cf0  /*ret?*/
    mvret Close();   // @000549a0  /*ret?*/
    mvret Append(PTR, unsigned long);   // @00054ba0  /*ret?*/
    mvret IsOpen() const;   // @00055330  /*ret?*/
    mvret SeekE(long);   // @00054cb0  /*ret?*/
    mvret SeekB(long);   // @00054c30  /*ret?*/
    mvret OpenW();   // @00054900  /*ret?*/
    mvret GetLength();   // @000549e0  /*ret?*/
    mvret OpenRW();   // @00054950  /*ret?*/
    mvret Write(const void *, unsigned long);   // @00054d30  /*ret?*/
    mvret SetBlockMode(bool);   // @00054d70  /*ret?*/
    mvret IsBlockMode();   // @00054d80  /*ret?*/
    mvret Rename(const char *);   // @000552c0  /*ret?*/
    mvret InitFileSystem();   // @00055350  /*ret?*/
    mvret OpenR(bool);   // @00054880  /*ret?*/
    mvret Delete();   // @000552f0  /*ret?*/
    mvret GetPosition();   // @00054c00  /*ret?*/
    mvret SeekC(long);   // @00054c70  /*ret?*/
    mvret Load(bool);   // @00054ac0  /*ret?*/
    mvret IsEnd() const;   // @00055310  /*ret?*/
    mvret Save(PTR, unsigned long);   // @00054b40  /*ret?*/
};

// ==== cFileSystem [polymorphic, rtti] ==========================
class cFileSystem {
public:
};

// ==== cFont [polymorphic, rtti] ================================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cFont {
public:
    cFont();   // @00059090
    ~cFont();   // @00059100
    mvret SaveMFT(char *);   // @00057ec0  /*ret?*/
};

// ==== cGD [polymorphic, rtti] ==================================
class cGD {
public:
    cGD(PTR, const cDimension &, unsigned long);   // @0008af20
    ~cGD();   // @0008af00
    mvret TileAlfa(const cRectangle &, cBitmap &, const cRectangle &);   // @000863d0  /*ret?*/
    mvret PutBitmapLMask(cBitmap &, cBitmap &, const tPoint<long> &, const cRectangle &, cPaletteFull *);   // @0008a9a0  /*ret?*/
    mvret PutAnimBitmapSub(cAnimBitmap &, unsigned long, const tPoint<long> &, cPaletteFull *);   // @00088560  /*ret?*/
    mvret PutBitmapAdd(cBitmap &, const tPoint<long> &, const cRectangle &, cPaletteFull *);   // @0008a120  /*ret?*/
    mvret Frame(const cRectangle &, cColor);   // @000884c0  /*ret?*/
    mvret FrameAlpha(const cRectangle &, unsigned long, cColor, bool, const cRectangle &);   // @00086940  /*ret?*/
    mvret PutBitmapSub(cBitmap &, const tPoint<long> &, cPaletteFull *);   // @00089f70  /*ret?*/
    mvret PutBitmapAMask(cBitmap &, const tPoint<long> &, const cRectangle &, cPaletteFull *);   // @00089c50  /*ret?*/
    mvret PutAnimBitmapLMask(cAnimBitmap &, unsigned long, cBitmap &, const tPoint<long> &, const cRectangle &, cPaletteFull *);   // @00089200  /*ret?*/
    mvret SaveBitmap(cBitmap &, const tPoint<long> &);   // @00089780  /*ret?*/
    mvret PutAnimBitmap(cAnimBitmap &, unsigned long, const tPoint<long> &, cPaletteFull *);   // @00089400  /*ret?*/
    mvret BoxAlfa(const cRectangle &, cBitmap &, unsigned long, cColor, bool, const cRectangle &);   // @000867a0  /*ret?*/
    mvret Fill(const cRectangle &, cColor);   // @00088520  /*ret?*/
    mvret Tile(const cRectangle &, cBitmap &, const cRectangle &);   // @00086060  /*ret?*/
    mvret PutAnimBitmapSub(cAnimBitmap &, unsigned long, const tPoint<long> &, const cRectangle &, cPaletteFull *);   // @00088730  /*ret?*/
    mvret VLine_(const tPoint<long> &, long, cColor, const cRectangle &);   // @000868a0  /*ret?*/
    mvret HLine_(const tPoint<long> &, long, cColor, const cRectangle &);   // @00086800  /*ret?*/
    mvret PutBitmapSub(cBitmap &, const tPoint<long> &, const cRectangle &, cPaletteFull *);   // @00089de0  /*ret?*/
    mvret GetAddr();   // @0008aee0  /*ret?*/
    mvret PutBitmapAlfa(cBitmap &, const tPoint<long> &, const cRectangle &, cPaletteFull *);   // @0008a460  /*ret?*/
    mvret SaveBitmap(cBitmap &, const tPoint<long> &, const cRectangle &);   // @00089920  /*ret?*/
    mvret PutBitmapAdd(cBitmap &, const tPoint<long> &, cPaletteFull *);   // @0008a2b0  /*ret?*/
    mvret PutAnimBitmapLMask(cAnimBitmap &, unsigned long, cBitmap &, const tPoint<long> &, cPaletteFull *);   // @00088fe0  /*ret?*/
    mvret PutAnimBitmapAdd(cAnimBitmap &, unsigned long, const tPoint<long> &, const cRectangle &, cPaletteFull *);   // @00088ab0  /*ret?*/
    mvret PutBitmapAMask(cBitmap &, const tPoint<long> &, cPaletteFull *);   // @00089aa0  /*ret?*/
    mvret PutAnimBitmapAlfa(cAnimBitmap &, unsigned long, const tPoint<long> &, cPaletteFull *);   // @00088c60  /*ret?*/
    mvret PutBitmap(cBitmap &, const tPoint<long> &, const cRectangle &, cPaletteFull *);   // @0008ad30  /*ret?*/
    mvret PutBitmapLMask(cBitmap &, cBitmap &, const tPoint<long> &, cPaletteFull *);   // @0008a7a0  /*ret?*/
    mvret PutAnimBitmapAlfa(cAnimBitmap &, unsigned long, const tPoint<long> &, const cRectangle &, cPaletteFull *);   // @00088e30  /*ret?*/
    mvret VLineAdd(tPoint<long>, unsigned long, cColor, const cRectangle &);   // @00088110  /*ret?*/
    mvret VLineSub(tPoint<long>, unsigned long, cColor, const cRectangle &);   // @00087fd0  /*ret?*/
    mvret GetDimension();   // @0008af60  /*ret?*/
    mvret HLineAdd(tPoint<long>, unsigned long, cColor, const cRectangle &);   // @000881b0  /*ret?*/
    mvret PutBitmapAlfa(cBitmap &, const tPoint<long> &, cPaletteFull *);   // @0008a5f0  /*ret?*/
    mvret PutBitmap(cBitmap &, const tPoint<long> &, cPaletteFull *);   // @0008ab80  /*ret?*/
    mvret GetBMType();   // @0008aed0  /*ret?*/
    mvret Box(const cRectangle &, cBitmap &, unsigned long, cColor, bool, const cRectangle &);   // @00086740  /*ret?*/
    mvret PutAnimBitmapAdd(cAnimBitmap &, unsigned long, const tPoint<long> &, cPaletteFull *);   // @000888e0  /*ret?*/
    mvret HLineClip(tPoint<long> &, unsigned long &, const cRectangle &);   // @000882f0  /*ret?*/
    mvret GetPitch();   // @0008aec0  /*ret?*/
    mvret PutAnimBitmap(cAnimBitmap &, unsigned long, const tPoint<long> &, const cRectangle &, cPaletteFull *);   // @000895d0  /*ret?*/
    mvret Clear(cColor);   // @00087ea0  /*ret?*/
    mvret VLineClip(tPoint<long> &, unsigned long &, const cRectangle &);   // @00088250  /*ret?*/
    mvret CRefresh(const cRectangle &);   // @00087ef0  /*ret?*/
    mvret HLineSub(tPoint<long>, unsigned long, cColor, const cRectangle &);   // @00088070  /*ret?*/
    mvret Frame(const cRectangle &, cColor, const cRectangle &);   // @00088390  /*ret?*/
};

// ==== cGD_LFB15 [polymorphic, rtti] ============================
class cGD_LFB15 {
public:
    cGD_LFB15(PTR, const cDimension &, unsigned long);   // @00065be0
    ~cGD_LFB15();   // @00065c60
    mvret PutAnimBitmapAdd_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00064660  /*ret?*/
    mvret CalcModulo(unsigned long);   // @00065bc0  /*ret?*/
    mvret HLineSub(const tPoint<long> &, unsigned long, cColor);   // @000659b0  /*ret?*/
    mvret PutPixel(const tPoint<long> &, cColor);   // @000657a0  /*ret?*/
    mvret PutAnimBitmap_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @000628d0  /*ret?*/
    mvret PutBitmapAlfa_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00060c30  /*ret?*/
    mvret Fill_I(cColor, const cRectangle &);   // @00065560  /*ret?*/
    mvret PutBitmapAdd_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @000612d0  /*ret?*/
    mvret PutBitmapSub_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00061970  /*ret?*/
    mvret PutBitmapAMask_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00062210  /*ret?*/
    mvret Line(const tPoint<long> &, const tPoint<long> &, cColor);   // @00065b80  /*ret?*/
    mvret Refresh(const cRectangle &);   // @00065b90  /*ret?*/
    mvret PutAnimBitmapAlfa_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00063cf0  /*ret?*/
    mvret PutBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0005fd50  /*ret?*/
    mvret VLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @00065b00  /*ret?*/
    mvret SaveBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &);   // @00062010  /*ret?*/
    mvret HLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @00065a90  /*ret?*/
    mvret PutBitmapLMask_I(cBitmap &, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00060570  /*ret?*/
    mvret FillAdd(cColor, const cRectangle &);   // @000655f0  /*ret?*/
    mvret VLine(const tPoint<long> &, unsigned long, cColor);   // @00065860  /*ret?*/
    mvret FillSub(cColor, const cRectangle &);   // @00065680  /*ret?*/
    mvret VLineAdd(const tPoint<long> &, unsigned long, cColor);   // @00065940  /*ret?*/
    mvret CalcAddr(const tPoint<long> &);   // @00065ba0  /*ret?*/
    mvret HLineAdd(const tPoint<long> &, unsigned long, cColor);   // @000658d0  /*ret?*/
    mvret PutAnimBitmapLMask_I(cAnimBitmap &, unsigned long, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00063240  /*ret?*/
    mvret VLineSub(const tPoint<long> &, unsigned long, cColor);   // @00065a20  /*ret?*/
    mvret PutAnimBitmapSub_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00064fd0  /*ret?*/
    mvret FillAlfa(cColor, const cRectangle &);   // @00065710  /*ret?*/
    mvret HLine(const tPoint<long> &, unsigned long, cColor);   // @000657f0  /*ret?*/
    mvret IsAsyncRefreshCapable();   // @00065b70  /*ret?*/
};

// ==== cGD_LFB16 [polymorphic, rtti] ============================
class cGD_LFB16 {
public:
    cGD_LFB16(PTR, const cDimension &, unsigned long);   // @0006bb30
    ~cGD_LFB16();   // @0006bbb0
    mvret VLine(const tPoint<long> &, unsigned long, cColor);   // @0006b7b0  /*ret?*/
    mvret VLineSub(const tPoint<long> &, unsigned long, cColor);   // @0006b970  /*ret?*/
    mvret HLine(const tPoint<long> &, unsigned long, cColor);   // @0006b740  /*ret?*/
    mvret PutAnimBitmapLMask_I(cAnimBitmap &, unsigned long, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00069190  /*ret?*/
    mvret FillSub(cColor, const cRectangle &);   // @0006b5d0  /*ret?*/
    mvret HLineSub(const tPoint<long> &, unsigned long, cColor);   // @0006b900  /*ret?*/
    mvret CalcAddr(const tPoint<long> &);   // @0006baf0  /*ret?*/
    mvret PutAnimBitmapAlfa_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00069c40  /*ret?*/
    mvret PutAnimBitmapSub_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006af20  /*ret?*/
    mvret FillAdd(cColor, const cRectangle &);   // @0006b540  /*ret?*/
    mvret Refresh(const cRectangle &);   // @0006bae0  /*ret?*/
    mvret PutBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00065c80  /*ret?*/
    mvret PutPixel(const tPoint<long> &, cColor);   // @0006b6f0  /*ret?*/
    mvret HLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @0006b9e0  /*ret?*/
    mvret CalcModulo(unsigned long);   // @0006bb10  /*ret?*/
    mvret VLineAdd(const tPoint<long> &, unsigned long, cColor);   // @0006b890  /*ret?*/
    mvret Line(const tPoint<long> &, const tPoint<long> &, cColor);   // @0006bad0  /*ret?*/
    mvret SaveBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &);   // @00067f50  /*ret?*/
    mvret IsAsyncRefreshCapable();   // @0006bac0  /*ret?*/
    mvret FillAlfa(cColor, const cRectangle &);   // @0006b660  /*ret?*/
    mvret PutBitmapAdd_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @000671f0  /*ret?*/
    mvret PutAnimBitmap_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00068820  /*ret?*/
    mvret PutAnimBitmapAdd_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006a5b0  /*ret?*/
    mvret HLineAdd(const tPoint<long> &, unsigned long, cColor);   // @0006b820  /*ret?*/
    mvret PutBitmapAMask_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00068160  /*ret?*/
    mvret PutBitmapLMask_I(cBitmap &, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00066480  /*ret?*/
    mvret PutBitmapAlfa_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00066b40  /*ret?*/
    mvret Fill_I(cColor, const cRectangle &);   // @0006b4b0  /*ret?*/
    mvret PutBitmapSub_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @000678a0  /*ret?*/
    mvret VLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @0006ba50  /*ret?*/
};

// ==== cGD_LFB24 [polymorphic, rtti] ============================
class cGD_LFB24 {
public:
    cGD_LFB24(PTR, const cDimension &, unsigned long);   // @0006f850
    ~cGD_LFB24();   // @0006f8d0
    mvret Line(const tPoint<long> &, const tPoint<long> &, cColor);   // @0006f7f0  /*ret?*/
    mvret PutAnimBitmapLMask_I(cAnimBitmap &, unsigned long, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006e1e0  /*ret?*/
    mvret CalcAddr(const tPoint<long> &);   // @0006f810  /*ret?*/
    mvret PutAnimBitmapSub_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006ef90  /*ret?*/
    mvret PutPixel(const tPoint<long> &, cColor);   // @0006f560  /*ret?*/
    mvret VLineAdd(const tPoint<long> &, unsigned long, cColor);   // @0006f670  /*ret?*/
    mvret VLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @0006f790  /*ret?*/
    mvret PutBitmapSub_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006cfd0  /*ret?*/
    mvret PutBitmapAdd_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006cad0  /*ret?*/
    mvret VLine(const tPoint<long> &, unsigned long, cColor);   // @0006f5e0  /*ret?*/
    mvret PutBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006bbd0  /*ret?*/
    mvret VLineSub(const tPoint<long> &, unsigned long, cColor);   // @0006f700  /*ret?*/
    mvret Fill_I(cColor, const cRectangle &);   // @0006f360  /*ret?*/
    mvret HLineAdd(const tPoint<long> &, unsigned long, cColor);   // @0006f630  /*ret?*/
    mvret PutAnimBitmapAdd_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006ebc0  /*ret?*/
    mvret HLineSub(const tPoint<long> &, unsigned long, cColor);   // @0006f6c0  /*ret?*/
    mvret PutBitmapAlfa_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006c5d0  /*ret?*/
    mvret PutBitmapLMask_I(cBitmap &, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006c0d0  /*ret?*/
    mvret FillSub(cColor, const cRectangle &);   // @0006f460  /*ret?*/
    mvret PutBitmapAMask_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006d6d0  /*ret?*/
    mvret FillAdd(cColor, const cRectangle &);   // @0006f3e0  /*ret?*/
    mvret Refresh(const cRectangle &);   // @0006f800  /*ret?*/
    mvret PutAnimBitmap_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006dbd0  /*ret?*/
    mvret HLine(const tPoint<long> &, unsigned long, cColor);   // @0006f5a0  /*ret?*/
    mvret PutAnimBitmapAlfa_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006e7f0  /*ret?*/
    mvret CalcModulo(unsigned long);   // @0006f830  /*ret?*/
    mvret SaveBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &);   // @0006d4d0  /*ret?*/
    mvret FillAlfa(cColor, const cRectangle &);   // @0006f4e0  /*ret?*/
    mvret HLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @0006f750  /*ret?*/
    mvret IsAsyncRefreshCapable();   // @0006f7e0  /*ret?*/
};

// ==== cGD_LFB32 [polymorphic, rtti] ============================
class cGD_LFB32 {
public:
    cGD_LFB32(PTR, const cDimension &, unsigned long);   // @00074c90
    ~cGD_LFB32();   // @00074d10
    mvret VLineAdd(const tPoint<long> &, unsigned long, cColor);   // @00074a90  /*ret?*/
    mvret PutBitmapSub_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00071590  /*ret?*/
    mvret PutBitmapAMask_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00071e10  /*ret?*/
    mvret CalcAddr(const tPoint<long> &);   // @00074c50  /*ret?*/
    mvret PutPixel(const tPoint<long> &, cColor);   // @00074960  /*ret?*/
    mvret SaveBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &);   // @00071c10  /*ret?*/
    mvret VLine(const tPoint<long> &, unsigned long, cColor);   // @000749f0  /*ret?*/
    mvret PutAnimBitmapLMask_I(cAnimBitmap &, unsigned long, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00072de0  /*ret?*/
    mvret FillAdd(cColor, const cRectangle &);   // @000747e0  /*ret?*/
    mvret VLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @00074bd0  /*ret?*/
    mvret PutBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @0006f8f0  /*ret?*/
    mvret Line(const tPoint<long> &, const tPoint<long> &, cColor);   // @00074c30  /*ret?*/
    mvret HLine(const tPoint<long> &, unsigned long, cColor);   // @000749a0  /*ret?*/
    mvret PutBitmapLMask_I(cBitmap &, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @000700c0  /*ret?*/
    mvret VLineSub(const tPoint<long> &, unsigned long, cColor);   // @00074b30  /*ret?*/
    mvret IsAsyncRefreshCapable();   // @00074c20  /*ret?*/
    mvret PutAnimBitmapSub_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @000741f0  /*ret?*/
    mvret FillAlfa(cColor, const cRectangle &);   // @000748e0  /*ret?*/
    mvret Fill_I(cColor, const cRectangle &);   // @00074760  /*ret?*/
    mvret PutAnimBitmapAdd_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00073c80  /*ret?*/
    mvret PutBitmapAlfa_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00070890  /*ret?*/
    mvret Refresh(const cRectangle &);   // @00074c40  /*ret?*/
    mvret PutAnimBitmap_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @000724b0  /*ret?*/
    mvret CalcModulo(unsigned long);   // @00074c70  /*ret?*/
    mvret HLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @00074b80  /*ret?*/
    mvret PutAnimBitmapAlfa_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00073710  /*ret?*/
    mvret HLineSub(const tPoint<long> &, unsigned long, cColor);   // @00074ae0  /*ret?*/
    mvret FillSub(cColor, const cRectangle &);   // @00074860  /*ret?*/
    mvret HLineAdd(const tPoint<long> &, unsigned long, cColor);   // @00074a40  /*ret?*/
    mvret PutBitmapAdd_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00070f10  /*ret?*/
};

// ==== cGD_LFB8 [polymorphic, rtti] =============================
class cGD_LFB8 {
public:
    cGD_LFB8(PTR, const cDimension &, unsigned long);   // @00076070
    ~cGD_LFB8();   // @000760f0
    mvret VLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @00075d60  /*ret?*/
    mvret FillAdd(cColor, const cRectangle &);   // @00075d20  /*ret?*/
    mvret PutBitmapLMask_I(cBitmap &, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00074ef0  /*ret?*/
    mvret CalcAddr(const tPoint<long> &);   // @00076040  /*ret?*/
    mvret Refresh(const cRectangle &);   // @00076020  /*ret?*/
    mvret Fill_I(cColor, const cRectangle &);   // @00075b70  /*ret?*/
    mvret VLineSub(const tPoint<long> &, unsigned long, cColor);   // @00075de0  /*ret?*/
    mvret PutBitmapAMask_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @000752b0  /*ret?*/
    mvret Line(const tPoint<long> &, const tPoint<long> &, cColor);   // @00075f70  /*ret?*/
    mvret PutAnimBitmapSub_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00075f90  /*ret?*/
    mvret HLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @00075da0  /*ret?*/
    mvret HLineSub(const tPoint<long> &, unsigned long, cColor);   // @00075e20  /*ret?*/
    mvret VLine(const tPoint<long> &, unsigned long, cColor);   // @00075c50  /*ret?*/
    mvret FillSub(cColor, const cRectangle &);   // @00075ce0  /*ret?*/
    mvret IsAsyncRefreshCapable();   // @00075c90  /*ret?*/
    mvret PutAnimBitmapLMask_I(cAnimBitmap &, unsigned long, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00075800  /*ret?*/
    mvret PutAnimBitmapAdd_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00075fc0  /*ret?*/
    mvret PutAnimBitmap_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00075490  /*ret?*/
    mvret PutBitmapAdd_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00075f10  /*ret?*/
    mvret PutBitmapSub_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00075ee0  /*ret?*/
    mvret Color2Index(cColor);   // @00076030  /*ret?*/
    mvret HLine(const tPoint<long> &, unsigned long, cColor);   // @00075c10  /*ret?*/
    mvret PutAnimBitmapAlfa_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00075ff0  /*ret?*/
    mvret PutPixel(const tPoint<long> &, cColor);   // @00075be0  /*ret?*/
    mvret HLineAdd(const tPoint<long> &, unsigned long, cColor);   // @00075ea0  /*ret?*/
    mvret SaveBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &);   // @000750b0  /*ret?*/
    mvret VLineAdd(const tPoint<long> &, unsigned long, cColor);   // @00075e60  /*ret?*/
    mvret PutBitmapAlfa_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00075f40  /*ret?*/
    mvret PutBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00074d30  /*ret?*/
    mvret CalcModulo(unsigned long);   // @00076060  /*ret?*/
    mvret FillAlfa(cColor, const cRectangle &);   // @00075ca0  /*ret?*/
};

// ==== cGD_Minimal [polymorphic, rtti] ==========================
class cGD_Minimal {
public:
    cGD_Minimal(const cDimension &, unsigned long);   // @00087df0
    ~cGD_Minimal();   // @00087e80
    mvret VLine(const tPoint<long> &, unsigned long, cColor);   // @00087c30  /*ret?*/
    mvret Fill_I(cColor, const cRectangle &);   // @00087cf0  /*ret?*/
    mvret HLineAdd(const tPoint<long> &, unsigned long, cColor);   // @00087c10  /*ret?*/
    mvret FillAlfa(cColor, const cRectangle &);   // @00087c90  /*ret?*/
    mvret PutAnimBitmapAlfa_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00087d50  /*ret?*/
    mvret VLineAdd(const tPoint<long> &, unsigned long, cColor);   // @00087bf0  /*ret?*/
    mvret NoSupported();   // @00087e30  /*ret?*/
    mvret VLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @00087b70  /*ret?*/
    mvret PutBitmapAdd_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00087db0  /*ret?*/
    mvret HLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @00087b90  /*ret?*/
    mvret PutAnimBitmapAdd_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00087d30  /*ret?*/
    mvret PutPixel(const tPoint<long> &, cColor);   // @00087c70  /*ret?*/
    mvret VLineSub(const tPoint<long> &, unsigned long, cColor);   // @00087bb0  /*ret?*/
    mvret FillAdd(cColor, const cRectangle &);   // @00087cd0  /*ret?*/
    mvret Line(const tPoint<long> &, const tPoint<long> &, cColor);   // @00087b50  /*ret?*/
    mvret PutAnimBitmapSub_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00087d10  /*ret?*/
    mvret FillSub(cColor, const cRectangle &);   // @00087cb0  /*ret?*/
    mvret HLineSub(const tPoint<long> &, unsigned long, cColor);   // @00087bd0  /*ret?*/
    mvret HLine(const tPoint<long> &, unsigned long, cColor);   // @00087c50  /*ret?*/
    mvret PutAnimBitmap_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00087d70  /*ret?*/
    mvret PutBitmapSub_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00087d90  /*ret?*/
    mvret PutBitmapAlfa_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00087dd0  /*ret?*/
};

// ==== cGD_SFB16 [polymorphic, rtti] ============================
class cGD_SFB16 {
public:
    cGD_SFB16(PTR, const cDimension &, unsigned long);   // @000776d0
    ~cGD_SFB16();   // @00077750
    mvret PutBitmapLMask_I(cBitmap &, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00076310  /*ret?*/
    mvret CalcAddr(const tPoint<long> &);   // @00077690  /*ret?*/
    mvret PutBitmapAdd_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00077610  /*ret?*/
    mvret PutAnimBitmap_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00076910  /*ret?*/
    mvret HLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @00077550  /*ret?*/
    mvret PutAnimBitmapLMask_I(cAnimBitmap &, unsigned long, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00076eb0  /*ret?*/
    mvret FillAlfa(cColor, const cRectangle &);   // @00077470  /*ret?*/
    mvret Refresh(const cRectangle &);   // @00077680  /*ret?*/
    mvret PutBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00076110  /*ret?*/
    mvret HLineAdd(const tPoint<long> &, unsigned long, cColor);   // @000775d0  /*ret?*/
    mvret Fill_I(cColor, const cRectangle &);   // @00077450  /*ret?*/
    mvret VLine(const tPoint<long> &, unsigned long, cColor);   // @00077660  /*ret?*/
    mvret PutPixel(const tPoint<long> &, cColor);   // @00077460  /*ret?*/
    mvret HLine(const tPoint<long> &, unsigned long, cColor);   // @00077670  /*ret?*/
    mvret HLineSub(const tPoint<long> &, unsigned long, cColor);   // @00077590  /*ret?*/
    mvret PutAnimBitmapSub_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @000774d0  /*ret?*/
    mvret PutBitmapAlfa_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00077630  /*ret?*/
    mvret CalcModulo(unsigned long);   // @000776b0  /*ret?*/
    mvret PutAnimBitmapAlfa_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00077510  /*ret?*/
    mvret FillAdd(cColor, const cRectangle &);   // @000774b0  /*ret?*/
    mvret PutBitmapAMask_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00076710  /*ret?*/
    mvret PutAnimBitmapAdd_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @000774f0  /*ret?*/
    mvret VLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @00077530  /*ret?*/
    mvret VLineAdd(const tPoint<long> &, unsigned long, cColor);   // @000775b0  /*ret?*/
    mvret FillSub(cColor, const cRectangle &);   // @00077490  /*ret?*/
    mvret SaveBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &);   // @00076510  /*ret?*/
    mvret Line(const tPoint<long> &, const tPoint<long> &, cColor);   // @00077650  /*ret?*/
    mvret VLineSub(const tPoint<long> &, unsigned long, cColor);   // @00077570  /*ret?*/
    mvret PutBitmapSub_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @000775f0  /*ret?*/
};

// ==== cGD_SFB8 [polymorphic, rtti] =============================
class cGD_SFB8 {
public:
    cGD_SFB8(PTR, const cDimension &, unsigned long);   // @00078930
    ~cGD_SFB8();   // @000789b0
    mvret VLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @00078790  /*ret?*/
    mvret PutBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00077770  /*ret?*/
    mvret PutBitmapAdd_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00078870  /*ret?*/
    mvret Line(const tPoint<long> &, const tPoint<long> &, cColor);   // @000788b0  /*ret?*/
    mvret PutAnimBitmapAlfa_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00078770  /*ret?*/
    mvret Refresh(const cRectangle &);   // @000788e0  /*ret?*/
    mvret FillAlfa(cColor, const cRectangle &);   // @000786d0  /*ret?*/
    mvret CalcAddr(const tPoint<long> &);   // @00078900  /*ret?*/
    mvret PutBitmapAlfa_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00078890  /*ret?*/
    mvret Fill_I(cColor, const cRectangle &);   // @00078600  /*ret?*/
    mvret FillSub(cColor, const cRectangle &);   // @000786f0  /*ret?*/
    mvret PutBitmapSub_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00078850  /*ret?*/
    mvret PutAnimBitmapLMask_I(cAnimBitmap &, unsigned long, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00078290  /*ret?*/
    mvret FillAdd(cColor, const cRectangle &);   // @00078710  /*ret?*/
    mvret PutBitmapLMask_I(cBitmap &, cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00077960  /*ret?*/
    mvret HLineSub(const tPoint<long> &, unsigned long, cColor);   // @000787f0  /*ret?*/
    mvret PutAnimBitmapAdd_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00078750  /*ret?*/
    mvret VLine(const tPoint<long> &, unsigned long, cColor);   // @000788c0  /*ret?*/
    mvret CalcModulo(unsigned long);   // @00078920  /*ret?*/
    mvret PutPixel(const tPoint<long> &, cColor);   // @00078680  /*ret?*/
    mvret PutBitmapAMask_I(cBitmap &, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00077d40  /*ret?*/
    mvret HLineAdd(const tPoint<long> &, unsigned long, cColor);   // @00078830  /*ret?*/
    mvret VLineSub(const tPoint<long> &, unsigned long, cColor);   // @000787d0  /*ret?*/
    mvret PutAnimBitmapSub_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00078730  /*ret?*/
    mvret VLineAdd(const tPoint<long> &, unsigned long, cColor);   // @00078810  /*ret?*/
    mvret SaveBitmap_I(cBitmap &, const cRectangle &, const tPoint<long> &);   // @00077b50  /*ret?*/
    mvret PutAnimBitmap_I(cAnimBitmap &, unsigned long, const cRectangle &, const tPoint<long> &, cPaletteFull *);   // @00077f20  /*ret?*/
    mvret HLineAlfa(const tPoint<long> &, unsigned long, cColor);   // @000787b0  /*ret?*/
    mvret HLine(const tPoint<long> &, unsigned long, cColor);   // @000788d0  /*ret?*/
    mvret Color2Index(cColor);   // @000788f0  /*ret?*/
};

// ==== cHList ===================================================
class cHList {
public:
    cHList();   // @0005ef10
    mvret GetLength() const;   // @0005edc0  /*ret?*/
    mvret AddFirst(cHNode *);   // @0005eeb0  /*ret?*/
    mvret DeleteList();   // @0005edf0  /*ret?*/
    mvret AddLast(cHNode *);   // @0005ee90  /*ret?*/
    mvret GetLast() const;   // @0005eed0  /*ret?*/
    mvret GetFirst() const;   // @0005eef0  /*ret?*/
};

// ==== cHNode [polymorphic, rtti] ===============================
class cHNode {
public:
    cHNode();   // @0005f170
    ~cHNode();   // @0005f200
    mvret GetPrev() const;   // @0005f130  /*ret?*/
    mvret AddNext(cHNode *);   // @0005f0b0  /*ret?*/
    mvret GetNofChildren() const;   // @0005eff0  /*ret?*/
    mvret AddChildFirst(cHNode *);   // @0005f050  /*ret?*/
    mvret GetFirstChild() const;   // @0005f110  /*ret?*/
    mvret GetNext() const;   // @0005f150  /*ret?*/
    mvret AddChildLast(cHNode *);   // @0005f020  /*ret?*/
    mvret GetParent() const;   // @0005f0e0  /*ret?*/
    mvret GetLastChild() const;   // @0005f0f0  /*ret?*/
    mvret DeleteChildren();   // @0005ef40  /*ret?*/
    mvret AddPrev(cHNode *);   // @0005f080  /*ret?*/
    mvret UnLink();   // @0005efb0  /*ret?*/
};

// ==== cHeap [rtti] =============================================
class cHeap {
public:
};

// ==== cHeapBlock ===============================================
class cHeapBlock {
public:
    mvret IsSeemsGood(unsigned long);   // @0005b600  /*ret?*/
    mvret Print(unsigned long);   // @0005b5e0  /*ret?*/
};

// ==== cHeap_Compatibility [polymorphic, rtti] ==================
class cHeap_Compatibility {
public:
    cHeap_Compatibility(unsigned long);   // @0005b670
    ~cHeap_Compatibility();   // @0005b6d0
    mvret CheckAll();   // @0005b770  /*ret?*/
    mvret GetMemoryTop();   // @0005b830  /*ret?*/
    mvret Alloc(unsigned long, const char *);   // @0005b850  /*ret?*/
    mvret Free(void *, const char *);   // @0005b8f0  /*ret?*/
    mvret CheckAllFatal(char *);   // @0005b7e0  /*ret?*/
    mvret List();   // @0005b710  /*ret?*/
};

// ==== cIPCBrowser [polymorphic, rtti] ==========================
class cIPCBrowser {
public:
    ~cIPCBrowser();   // @00097320
};

// ==== cIPCBrowser_IPX [polymorphic, rtti] ======================
class cIPCBrowser_IPX {
public:
    cIPCBrowser_IPX(unsigned long, cIPXSocket *);   // @00079b80
    ~cIPCBrowser_IPX();   // @00079c20
    mvret Refresh();   // @00079e50  /*ret?*/
    mvret GetSession(unsigned long);   // @00079e10  /*ret?*/
    mvret GetNumberOfSessions();   // @00079d30  /*ret?*/
    mvret IsNewServer(const cIPXAddress &);   // @00079ca0  /*ret?*/
};

// ==== cIPCBrowser_TCPIP [polymorphic, rtti] ====================
class cIPCBrowser_TCPIP {
public:
    ~cIPCBrowser_TCPIP();   // @00093db0
    mvret Refresh();   // @00093d50  /*ret?*/
    mvret GetNumberOfSessions();   // @00093d70  /*ret?*/
    mvret GetSession(unsigned long);   // @00093d60  /*ret?*/
};

// ==== cIPCO [polymorphic, rtti] ================================
class cIPCO {
public:
    cIPCO(bool, bool);   // @00078db0
    ~cIPCO();   // @00078d90
    mvret IsGaranteed();   // @00078d70  /*ret?*/
    mvret WriteFatal(PTR, unsigned long, bool, bool);   // @00078d20  /*ret?*/
    mvret Write(const void *, unsigned long);   // @00078aa0  /*ret?*/
    mvret IsInBlockMode();   // @00078d80  /*ret?*/
};

// ==== cIPCOStream2Block [polymorphic, rtti] ====================
class cIPCOStream2Block {
public:
    cIPCOStream2Block();   // @0007a380
    cIPCOStream2Block(cIPCO *, unsigned long, bool);   // @0007a3c0
    ~cIPCOStream2Block();   // @0007a070
    mvret Disconnect();   // @0007a2c0  /*ret?*/
    mvret SetBlockMode(bool);   // @0007a2e0  /*ret?*/
    mvret IsCollected();   // @0007a430  /*ret?*/
    mvret GetIPCO();   // @0007a330  /*ret?*/
    mvret IsBlockMode();   // @0007a310  /*ret?*/
    mvret Read();   // @0007a0d0  /*ret?*/
    mvret IsUsed();   // @0007a1b0  /*ret?*/
    mvret Read(void *, unsigned long);   // @0007a120  /*ret?*/
    mvret IsDataAvailable(const cTime_US *);   // @0007a1d0  /*ret?*/
    mvret Construct(cIPCO *, unsigned long, bool);   // @0007a340  /*ret?*/
    mvret Write(const void *, unsigned long, bool, bool);   // @0007a240  /*ret?*/
    mvret IsConnected();   // @0007a190  /*ret?*/
};

// ==== cIPCO_IPX [polymorphic, rtti] ============================
class cIPCO_IPX {
public:
    cIPCO_IPX();   // @00078eb0
    cIPCO_IPX(cIPXSocket *, const cIPXAddress &);   // @00078f20
    ~cIPCO_IPX();   // @00078fb0
    mvret Read(void *, unsigned long);   // @00079280  /*ret?*/
    mvret IsBlockMode();   // @00079480  /*ret?*/
    mvret IsUsed();   // @00079410  /*ret?*/
    mvret IsConnected();   // @000793f0  /*ret?*/
    mvret ProcPacket(const cTime_US *);   // @00079030  /*ret?*/
    mvret Connect(cIPXSocket *, const cIPXAddress &);   // @00079000  /*ret?*/
    mvret SetBlockMode(bool);   // @00079470  /*ret?*/
    mvret Write(const void *, unsigned long, bool, bool);   // @00079360  /*ret?*/
    mvret GetPort();   // @0007a030  /*ret?*/
    mvret IsDataAvailable(const cTime_US *);   // @00079170  /*ret?*/
    mvret Disconnect();   // @00079430  /*ret?*/
};

// ==== cIPCO_TCPIP [polymorphic, rtti] ==========================
class cIPCO_TCPIP {
public:
    cIPCO_TCPIP();   // @00093ed0
    cIPCO_TCPIP(const char *, unsigned long);   // @000931e0
    ~cIPCO_TCPIP();   // @00093e90
    mvret SetBlockMode(bool);   // @000934f0  /*ret?*/
    mvret SetConnectionID(int);   // @00093e70  /*ret?*/
    mvret Disconnect();   // @00093550  /*ret?*/
    mvret InternalDisconnect();   // @00093510  /*ret?*/
    mvret IsDataAvailable(const cTime_US *);   // @000932f0  /*ret?*/
    mvret Write(const void *, unsigned long, bool, bool);   // @00093470  /*ret?*/
    mvret GetConnectionID();   // @00093e60  /*ret?*/
    mvret IsUsed();   // @00093590  /*ret?*/
    mvret Read(void *, unsigned long);   // @000933d0  /*ret?*/
    mvret IsBlockMode();   // @00093500  /*ret?*/
    mvret IsConnected();   // @00093570  /*ret?*/
};

// ==== cIPCServer [polymorphic, rtti] ===========================
class cIPCServer {
public:
    ~cIPCServer();   // @00078cd0
    mvret Broadcast(PTR, unsigned long, bool, bool);   // @00078a20  /*ret?*/
};

// ==== cIPCServer_IPX [polymorphic, rtti] =======================
class cIPCServer_IPX {
public:
    cIPCServer_IPX(cIPXSocket *, const char *);   // @000794c0
    ~cIPCServer_IPX();   // @000795c0
    mvret GetMaxConnections();   // @00079a40  /*ret?*/
    mvret GetFreeClient();   // @00079790  /*ret?*/
    mvret ProcPacket(const cTime_US *);   // @000797e0  /*ret?*/
    mvret IsDataAvailable(const cTime_US *);   // @00079610  /*ret?*/
    mvret Listen();   // @00079970  /*ret?*/
    mvret GetIPCO(unsigned long);   // @00079a10  /*ret?*/
};

// ==== cIPCServer_IPX::cClientInfo ==============================
class cIPCServer_IPX::cClientInfo {
public:
    cClientInfo();   // @00079490
};

// ==== cIPCServer_TCPIP [polymorphic, rtti] =====================
class cIPCServer_TCPIP {
public:
    cIPCServer_TCPIP(unsigned long, unsigned long, bool, int);   // @00093690
    ~cIPCServer_TCPIP();   // @00093820
    mvret Listen();   // @00093b10  /*ret?*/
    mvret GetMaxConnections();   // @00093ce0  /*ret?*/
    mvret GetIPCO(unsigned long);   // @00093cf0  /*ret?*/
    mvret IsDataAvailable(const cTime_US *);   // @000938d0  /*ret?*/
};

// ==== cIPCSession [polymorphic, rtti] ==========================
class cIPCSession {
public:
    cIPCSession(const char *);   // @000789d0
    ~cIPCSession();   // @00097220
};

// ==== cIPCSession_IPX [polymorphic, rtti] ======================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cIPCSession
class cIPCSession_IPX {
public:
    cIPCSession_IPX(const char *, const cIPXAddress &);   // @00079a50
    ~cIPCSession_IPX();   // @00079b00
    mvret Compare(const cIPXAddress &);   // @00079f60  /*ret?*/
    mvret StillAlive();   // @00079f10  /*ret?*/
    mvret Open();   // @00079b50  /*ret?*/
    mvret CheckLifesign(unsigned long);   // @00079f30  /*ret?*/
};

// ==== cIPCSession_TCPIP [polymorphic, rtti] ====================
class cIPCSession_TCPIP {
public:
    cIPCSession_TCPIP(const char *);   // @00093de0
    ~cIPCSession_TCPIP();   // @00093e40
    mvret Open();   // @00093dd0  /*ret?*/
};

// ==== cIPCSystem [polymorphic, rtti] ===========================
class cIPCSystem {
public:
    cIPCSystem();   // @00078c90
    ~cIPCSystem();   // @00078c70
    mvret CreateIPXServer(bool, unsigned long, const char *);   // @00078ad0  /*ret?*/
    mvret CreateIPXClient(bool, const cIPXAddress &);   // @00078b30  /*ret?*/
    mvret CreateIPXBrowser(unsigned long);   // @00078c00  /*ret?*/
};

// ==== cIPCSystem_Linux [polymorphic, rtti] =====================
class cIPCSystem_Linux {
public:
    ~cIPCSystem_Linux();   // @00094560
    mvret IsModemAvailable();   // @000943c0  /*ret?*/
    mvret IsSerialAvailable();   // @00094350  /*ret?*/
    mvret CreateTCPIPClient(bool, const char *, unsigned long);   // @00094430  /*ret?*/
    mvret CreateSerialServer(bool, unsigned long);   // @00094390  /*ret?*/
    mvret CreateTCPIPServer(bool, unsigned long, unsigned long);   // @00094510  /*ret?*/
    mvret CreateModemServer(bool, unsigned long);   // @000943f0  /*ret?*/
    mvret CreateModemClient(bool, const char *, unsigned long);   // @000943d0  /*ret?*/
    mvret IsIPXAvailable();   // @00094410  /*ret?*/
    mvret CreateSerialClient(bool, unsigned long);   // @00094360  /*ret?*/
    mvret CreateIPXSocket(unsigned long, unsigned long);   // @00093f40  /*ret?*/
    mvret IsTCPIPAvailable();   // @00094420  /*ret?*/
};

// ==== cIPXPacketHeader =========================================
class cIPXPacketHeader {
public:
    mvret Print();   // @00078e10  /*ret?*/
};

// ==== cIPXSocket [polymorphic, rtti] ===========================
class cIPXSocket {
public:
    ~cIPXSocket();   // @00098b70
};

// ==== cIPXSocket_Linux [polymorphic, rtti] =====================
class cIPXSocket_Linux {
public:
    cIPXSocket_Linux(int, unsigned long);   // @00094010
    ~cIPXSocket_Linux();   // @00094040
    mvret Broadcast(const void *, unsigned long, unsigned long);   // @00094180  /*ret?*/
    mvret Receive(void *, unsigned long, cIPXAddress &);   // @000941e0  /*ret?*/
    mvret Write(const void *, unsigned long);   // @00094160  /*ret?*/
    mvret GetEventDescriptor();   // @00094340  /*ret?*/
    mvret Send(const void *, unsigned long, const cIPXAddress &);   // @00094270  /*ret?*/
    mvret Read(void *, unsigned long);   // @00094140  /*ret?*/
    mvret IsDataAvailable(const cTime_US *);   // @00094060  /*ret?*/
    mvret Connect(const cIPXAddress &);   // @000942e0  /*ret?*/
};

// ==== cIntuition [polymorphic, rtti] ===========================
class cIntuition {
public:
    cIntuition();   // @0008d370
    ~cIntuition();   // @0008d4e0
    mvret GetMasterVO();   // @0008ef50  /*ret?*/
    mvret SetPointerPos(const tPoint<long> &) volatile;   // @0008f110  /*ret?*/
    mvret RefreshFocus();   // @0008dec0  /*ret?*/
    mvret SetQualifierState();   // @0008d590  /*ret?*/
    mvret GetScroll();   // @0008f0b0  /*ret?*/
    mvret GetFocus();   // @0008ef20  /*ret?*/
    mvret GetPointerPos();   // @0008f0e0  /*ret?*/
    mvret ClearKeyMatrix();   // @0008d620  /*ret?*/
    mvret RefreshPointerPos2(tPoint<long>);   // @0008d7b0  /*ret?*/
    mvret GetInputBuffer();   // @0008ef40  /*ret?*/
    mvret GetQualifierState();   // @0008d600  /*ret?*/
    mvret ActivateScreen(cScreen *);   // @0008d830  /*ret?*/
    mvret RefreshPointerPos(tPoint<long>);   // @0008d6b0  /*ret?*/
    mvret ProcessInputs();   // @0008d9f0  /*ret?*/
    mvret AddRequester(cVObject *, bool);   // @0008ef60  /*ret?*/
    mvret KeyMatrix(eKeyCode);   // @0008f090  /*ret?*/
    mvret GetActiveScreen();   // @0008f080  /*ret?*/
    mvret GetIPointerPos();   // @0008ef30  /*ret?*/
    mvret GetIMouseButtons();   // @0008dea0  /*ret?*/
    mvret TimerProc();   // @0008d640  /*ret?*/
};

// ==== cKeySequence =============================================
class cKeySequence {
public:
    cKeySequence();   // @0007ae10
    cKeySequence(const char *);   // @0007ae30
    mvret operator=(const cKeySequence &);   // @0007afa0  /*ret?*/
    mvret operator+=(eKeyCode);   // @0007aed0  /*ret?*/
    mvret operator=(const char *);   // @0007b010  /*ret?*/
    mvret Sequence() const;   // @0007b070  /*ret?*/
    mvret Equal(const char *);   // @0007af10  /*ret?*/
    mvret GetLength() const;   // @0007b080  /*ret?*/
    mvret Reset();   // @0007aeb0  /*ret?*/
};

// ==== cKeyboard [polymorphic, rtti] ============================
class cKeyboard {
public:
    cKeyboard(const long *);   // @0007aa20
    ~cKeyboard();   // @00097390
    mvret ConvertRawkey(long);   // @0007ade0  /*ret?*/
    mvret ReleaseAll();   // @0007ac40  /*ret?*/
    mvret Grab();   // @00097380  /*ret?*/
    mvret PushKey(eKeyCode, bool);   // @0007ab50  /*ret?*/
    mvret RawkeyToAscii(eKeyCode, unsigned char);   // @0007acf0  /*ret?*/
    mvret _GetKey();   // @0007ad20  /*ret?*/
    mvret Ungrab();   // @00097370  /*ret?*/
};

// ==== cLibKeyboard [polymorphic, rtti] =========================
class cLibKeyboard {
public:
    cLibKeyboard(const char *);   // @0005eaf0
    mvret MapAllSymbol();   // @0005ea80  /*ret?*/
};

// ==== cLibMouse [polymorphic, rtti] ============================
class cLibMouse {
public:
    cLibMouse(const char *);   // @0005ebc0
    mvret MapAllSymbol();   // @0005eb50  /*ret?*/
};

// ==== cLibPointer [polymorphic, rtti] ==========================
class cLibPointer {
public:
    cLibPointer(const char *);   // @0005ec90
    mvret MapAllSymbol();   // @0005ec20  /*ret?*/
};

// ==== cLibVVC [polymorphic, rtti] ==============================
class cLibVVC {
public:
    cLibVVC(const char *);   // @0005ed60
    mvret MapAllSymbol();   // @0005ecf0  /*ret?*/
};

// ==== cLibrary [polymorphic, rtti] =============================
class cLibrary {
public:
    mvret Close();   // @00098bc0  /*ret?*/
};

// ==== cLineEditor ==============================================
class cLineEditor {
public:
    cLineEditor(int);   // @00043850
    ~cLineEditor();   // @00043890
    mvret ShiftRight();   // @00043c90  /*ret?*/
    mvret InsertChar(char);   // @000439d0  /*ret?*/
    mvret MoveToNextWord();   // @00043b10  /*ret?*/
    mvret GetPos();   // @00044f20  /*ret?*/
    mvret RemoveToAll();   // @00043ac0  /*ret?*/
    mvret RemoveFromAll();   // @00043ae0  /*ret?*/
    mvret MoveToPrevWord();   // @00043b50  /*ret?*/
    mvret RemovePrevChar();   // @00043a00  /*ret?*/
    mvret Reset();   // @00043980  /*ret?*/
    mvret RemoveThisChar();   // @00043a30  /*ret?*/
    mvret DecrasePos();   // @00043c20  /*ret?*/
    mvret SetChar(char);   // @000439b0  /*ret?*/
    mvret MoveLeft();   // @00043bd0  /*ret?*/
    mvret MoveToBegin();   // @00043be0  /*ret?*/
    mvret IncrasePos();   // @00043c00  /*ret?*/
    mvret MoveRight();   // @00043bc0  /*ret?*/
    mvret ShiftLeft();   // @00043c40  /*ret?*/
    mvret GetLength();   // @00044f40  /*ret?*/
    mvret MoveToEnd();   // @00043bf0  /*ret?*/
    mvret Set(const char *);   // @000438c0  /*ret?*/
    mvret GetStop();   // @00044f30  /*ret?*/
    mvret SetStop(int);   // @00043950  /*ret?*/
    mvret RemoveThisWord();   // @00043a60  /*ret?*/
    mvret GetChar();   // @00043cf0  /*ret?*/
    mvret __opPCc() const;   // @00044f10  /*ret?*/
};

// ==== cLinuxTimer ==============================================
class cLinuxTimer {
public:
    mvret Cleanup();   // @00092350  /*ret?*/
    mvret DisableTimer();   // @00092400  /*ret?*/
    mvret Setup();   // @00092300  /*ret?*/
    mvret SetTimer(unsigned long);   // @00092380  /*ret?*/
};

// ==== cList ====================================================
class cList {
    // circular doubly-linked, two embedded sentinels (head +0x00, tail +0x0c) [AmigaOS MinList]
public:
    cList();   // @0005f410
    ~cList();   // @0005f3c0
    mvret GetLast() const;   // @0005f380  /*ret?*/
    mvret GetFirst() const;   // @0005f3a0  /*ret?*/
    mvret UnLinkList();   // @0005f2e0  /*ret?*/
    mvret GetLength() const;   // @0005f2b0  /*ret?*/
    mvret AddLast(cNode *);   // @0005f340  /*ret?*/
    mvret AddFirst(cNode *);   // @0005f360  /*ret?*/
    mvret DeleteList();   // @0005f270  /*ret?*/
};

// ==== cLocaleDataBase ==========================================
class cLocaleDataBase {
public:
    cLocaleDataBase();   // @0005f690
    ~cLocaleDataBase();   // @0005f6c0
    mvret ResolveKey(const char *);   // @0005fa50  /*ret?*/
    mvret GetDBName();   // @0005fae0  /*ret?*/
    mvret Read(PTR);   // @0005f8b0  /*ret?*/
    mvret Add(cLocaleEntry *);   // @0005fa90  /*ret?*/
    mvret Load(const char *, bool);   // @0005f720  /*ret?*/
};

// ==== cLocaleEntry [polymorphic, rtti] =========================
class cLocaleEntry {
public:
    cLocaleEntry(const char *);   // @0005f5f0
    ~cLocaleEntry();   // @0005fb60
    mvret GetText();   // @0005fb30  /*ret?*/
    mvret GetFileName();   // @0005faf0  /*ret?*/
    mvret SetFlags(unsigned char);   // @0005fbb0  /*ret?*/
    mvret IsFileName();   // @0005fb20  /*ret?*/
    mvret GetKey();   // @0005fbd0  /*ret?*/
    mvret SetText(const char *);   // @0005fbc0  /*ret?*/
    mvret IsSolved();   // @0005fb90  /*ret?*/
};

// ==== cMasterVO [polymorphic, rtti] ============================
class cMasterVO {
public:
    cMasterVO(const cRectangle &);   // @0008f8b0
    ~cMasterVO();   // @0008fab0
    mvret Process(sInput &);   // @0008f810  /*ret?*/
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @0008f8a0  /*ret?*/
    mvret Notify(const sVOMessage &);   // @0008f790  /*ret?*/
    mvret GetNotify();   // @0008f6b0  /*ret?*/
};

// ==== cMemBlock [polymorphic, rtti] ============================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cMemBlock {
    // layout (0x20): +0x00 next; +0x04 prev; +0x08 vtable(cNode);
    //   +0x0c data; +0x10 size; +0x14 lockCount; +0x18 priority(=0x7f); +0x1c vtable(cMemBlock_)
public:
    cMemBlock();   // @0007b6e0
    cMemBlock(unsigned long);   // @0007b640
    ~cMemBlock();   // @0007b5f0
    mvret Construct(unsigned long);   // @0007b5c0  /*ret?*/
    mvret Lock();   // @0007b500  /*ret?*/
    mvret Free();   // @0007b3d0  /*ret?*/
    mvret Flush();   // @0007b5a0  /*ret?*/
    mvret Validate();   // @0007b4b0  /*ret?*/
    mvret Alloc(unsigned long);   // @0007b430  /*ret?*/
};

// ==== cMemBlockPTR =============================================
class cMemBlockPTR {
public:
    cMemBlockPTR(cMemBlock_ *, unsigned long);   // @0007b830
    cMemBlockPTR(const cMemBlockPTR &);   // @0007b7f0
    cMemBlockPTR(cMemBlock_ *);   // @0007b870
    ~cMemBlockPTR();   // @0007b7d0
    mvret operator=(const cMemBlockPTR &);   // @0007b790  /*ret?*/
    mvret GetAddress();   // @0007b780  /*ret?*/
};

// ==== cMemBlock_ [polymorphic, rtti] ===========================
class cMemBlock_ {
    // payload base of cMemBlock: {data, size, lockCount}; owns Lock/Unlock/GetAddress
public:
    cMemBlock_(const cMemBlock_ &);   // @0007ba00
    cMemBlock_();   // @0007b9c0
    mvret GetAddress() const;   // @0007b8c0  /*ret?*/
    mvret SetPriority(unsigned short);   // @0007b910  /*ret?*/
    mvret Validate();   // @0007b990  /*ret?*/
    mvret GetAddress();   // @0007b8e0  /*ret?*/
    mvret _Validate();   // @0007b9f0  /*ret?*/
    mvret Lock();   // @0007b960  /*ret?*/
    mvret SetMBName(const char *);   // @0007b8b0  /*ret?*/
    mvret Unlock();   // @0007b950  /*ret?*/
    mvret IsValid();   // @0007b940  /*ret?*/
    mvret GetSize() const;   // @0007b900  /*ret?*/
    mvret IsLocked();   // @0007b920  /*ret?*/
};

// ==== cMemoryPipe ==============================================
class cMemoryPipe {
public:
    cMemoryPipe(unsigned long);   // @0007ba60
    ~cMemoryPipe();   // @0007ba90
    mvret PushObject(const void *, unsigned long);   // @0007bc40  /*ret?*/
    mvret PullObject(void *, unsigned long);   // @0007bdc0  /*ret?*/
    mvret Pull(void *, unsigned long);   // @0007bd00  /*ret?*/
    mvret EndPush();   // @0007bcf0  /*ret?*/
    mvret BeginPull(unsigned long);   // @0007be30  /*ret?*/
    mvret BeginPush(unsigned long);   // @0007bcd0  /*ret?*/
    mvret GetLargestFree();   // @0007baf0  /*ret?*/
    mvret GetFree();   // @0007bac0  /*ret?*/
    mvret IsEmpty();   // @0007bb40  /*ret?*/
    mvret Clear();   // @0007bb70  /*ret?*/
    mvret CancelPush();   // @0007bce0  /*ret?*/
    mvret CancelPull();   // @0007be40  /*ret?*/
    mvret EndPull();   // @0007be50  /*ret?*/
    mvret Push(const void *, unsigned long);   // @0007bb90  /*ret?*/
};

// ==== cMixer ===================================================
class cMixer {
public:
    cMixer();   // @000825e0
    ~cMixer();   // @000825b0
    mvret SetMasterVolume(const cVolume &);   // @00082590  /*ret?*/
    mvret GetBufferItemLength();   // @000824e0  /*ret?*/
    mvret MixUBM(unsigned long, cSoundChannel_SoftwareMix &);   // @000819a0  /*ret?*/
    mvret Fill(unsigned long, cSoundChannel_SoftwareMix &);   // @000816e0  /*ret?*/
    mvret Do(void *, unsigned long, tList<cSoundChannel_SoftwareMix> &, unsigned short);   // @00081560  /*ret?*/
    mvret GetNofChannels();   // @00082500  /*ret?*/
    mvret GetMasterVolume();   // @00082580  /*ret?*/
    mvret Mix(unsigned long, cSoundChannel_SoftwareMix &);   // @000817a0  /*ret?*/
    mvret Silence8(PTR, unsigned long);   // @000822c0  /*ret?*/
    mvret Silence16(PTR, unsigned long);   // @00082290  /*ret?*/
    mvret MixSWS_LE(unsigned long, cSoundChannel_SoftwareMix &);   // @00081c80  /*ret?*/
    mvret MixSWM_LE(unsigned long, cSoundChannel_SoftwareMix &);   // @00081f40  /*ret?*/
    mvret FillSWS_LE(unsigned long, cSoundChannel_SoftwareMix &);   // @00081ae0  /*ret?*/
    mvret ConvertSW(PTR, unsigned long, unsigned long);   // @00082060  /*ret?*/
    mvret ConvertUB(PTR, unsigned long, unsigned long);   // @00082190  /*ret?*/
    mvret SetBuffer(unsigned long, cSoundFormat, unsigned char);   // @00082520  /*ret?*/
    mvret FillUBM(unsigned long, cSoundChannel_SoftwareMix &);   // @00081860  /*ret?*/
    mvret FillSWM_LE(unsigned long, cSoundChannel_SoftwareMix &);   // @00081e20  /*ret?*/
};

// ==== cMouse [polymorphic, rtti] ===============================
class cMouse {
public:
    cMouse();   // @0007be60
    ~cMouse();   // @00097460
    mvret Grab();   // @00097450  /*ret?*/
    mvret IsGrabed();   // @00097430  /*ret?*/
    mvret GetNextEvent();   // @0007c020  /*ret?*/
    mvret EVENT_Buttons(unsigned char);   // @0007bf70  /*ret?*/
    mvret Ungrab();   // @00097440  /*ret?*/
    mvret EVENT_Move(const tPoint<long> &);   // @0007beb0  /*ret?*/
};

// ==== cMsgCenter ===============================================
class cMsgCenter {
public:
    cMsgCenter();   // @000855d0
    mvret AddFirstType(cMsgTypeNode *);   // @00085590  /*ret?*/
    mvret GetFirstType();   // @000855b0  /*ret?*/
    mvret DelReceiver(cMsgReceiver *);   // @000854a0  /*ret?*/
    mvret DelReceiver(cMsgReceiver *, char *);   // @00085400  /*ret?*/
    mvret FindType(char *);   // @000854f0  /*ret?*/
    mvret AddReceiver(cMsgReceiver *, unsigned long, char *, unsigned short, bool);   // @000852c0  /*ret?*/
    mvret Dump();   // @00085550  /*ret?*/
    mvret SendMsg(cMsgTypeNode *, PTR);   // @00085530  /*ret?*/
};

// ==== cMsgRecNode [polymorphic, rtti] ==========================
class cMsgRecNode {
public:
    cMsgRecNode(cMsgReceiver *, unsigned short, unsigned short);   // @000856e0
    ~cMsgRecNode();   // @00085760
    mvret GetTypeIndex();   // @000856b0  /*ret?*/
    mvret GetRecipient();   // @000856d0  /*ret?*/
    mvret GetPriority();   // @00085690  /*ret?*/
};

// ==== cMsgReceiver [polymorphic, rtti] =========================
class cMsgReceiver {
public:
    cMsgReceiver();   // @000857a0
    ~cMsgReceiver();   // @00084e70
    mvret Unsubscribe(char *);   // @00084f30  /*ret?*/
    mvret Receive(unsigned long, PTR);   // @00085790  /*ret?*/
    mvret Subscribe(char *, unsigned short, bool);   // @00084eb0  /*ret?*/
};

// ==== cMsgSender ===============================================
class cMsgSender {
public:
    cMsgSender(char *);   // @00085210
    mvret Send(PTR);   // @00085280  /*ret?*/
};

// ==== cMsgTypeNode [polymorphic, rtti] =========================
class cMsgTypeNode {
public:
    cMsgTypeNode(char *, bool);   // @00084f50
    ~cMsgTypeNode();   // @00085060
    mvret AddFirstRecipient(cMsgRecNode *);   // @000855f0  /*ret?*/
    mvret SendMsg(PTR);   // @000851d0  /*ret?*/
    mvret GetFirstRecipient();   // @00085620  /*ret?*/
    mvret AddRecipient(cMsgRecNode *);   // @000850e0  /*ret?*/
    mvret IsUnique();   // @00085640  /*ret?*/
    mvret DelRecipient(cMsgReceiver *);   // @00085170  /*ret?*/
    mvret GetType();   // @00085650  /*ret?*/
};

// ==== cNode [polymorphic, rtti] ================================
class cNode {
    // layout (0x0c): +0x00 next; +0x04 prev; +0x08 vtable  [AmigaOS Exec node]
public:
    cNode();   // @0005f5a0
    cNode(const cNode &);   // @0005f540
    cNode(cNode *, cNode *);   // @0005f580
    ~cNode();   // @0005f510
    mvret UnLink();   // @0005f440  /*ret?*/
    mvret GetNext() const;   // @0005f4f0  /*ret?*/
    mvret IsLinked() const;   // @0005f470  /*ret?*/
    mvret AddNext(cNode *);   // @0005f4b0  /*ret?*/
    mvret GetPrev() const;   // @0005f4d0  /*ret?*/
    mvret AddPrev(cNode *);   // @0005f490  /*ret?*/
};

// ==== cOldString ===============================================
class cOldString {
public:
    cOldString(const char *, unsigned long);   // @000848d0
    cOldString();   // @00084bf0
    cOldString(const char *);   // @00084870
    ~cOldString();   // @00084bc0
    mvret operator=(const char *);   // @00084930  /*ret?*/
    mvret operator[](unsigned long);   // @00084b90  /*ret?*/
    mvret operator+=(const char *);   // @00084b70  /*ret?*/
    mvret GetCString();   // @00084ba0  /*ret?*/
    mvret GetLength();   // @00084bb0  /*ret?*/
    mvret Cat(const char *);   // @000847b0  /*ret?*/
    mvret __eq(const char *) const;   // @00084b30  /*ret?*/
};

// ==== cPalette [polymorphic, rtti] =============================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cPalette {
public:
    cPalette(unsigned long);   // @0007fbf0
    cPalette();   // @0007fd00
    cPalette(const cPalette &);   // @0007e770
    ~cPalette();   // @0007fb70
    mvret operator[](unsigned long);   // @0007faa0  /*ret?*/
    mvret Brightness(unsigned long);   // @0007e970  /*ret?*/
    mvret Blue(unsigned long);   // @0007ea90  /*ret?*/
    mvret Red(unsigned long);   // @0007e9f0  /*ret?*/
    mvret GetColor(unsigned long) const;   // @0007fa50  /*ret?*/
    mvret _Validate();   // @0007fd80  /*ret?*/
    mvret SetSelfLock();   // @0007fb20  /*ret?*/
    mvret GetAmount();   // @0007f9f0  /*ret?*/
    mvret Green(unsigned long);   // @0007ea40  /*ret?*/
    mvret GetNearest(cColor) const;   // @0007eae0  /*ret?*/
    mvret Construct(unsigned long);   // @0007fae0  /*ret?*/
};

// ==== cPalette15 ===============================================
class cPalette15 {
public:
    cPalette15();   // @0007f8e0
    cPalette15(cPalette &);   // @0007f7a0
    mvret operator[](unsigned long);   // @0007f770  /*ret?*/
    mvret GetAddr();   // @0007f790  /*ret?*/
};

// ==== cPalette16 ===============================================
class cPalette16 {
public:
    cPalette16();   // @0007f710
    cPalette16(cPalette &);   // @0007f5d0
    mvret operator[](unsigned long);   // @0007f5a0  /*ret?*/
    mvret GetAddr();   // @0007f5c0  /*ret?*/
};

// ==== cPalette32 ===============================================
class cPalette32 {
public:
    cPalette32(cPalette &);   // @0007f420
    cPalette32();   // @0007f550
    mvret operator[](unsigned long);   // @0007f3f0  /*ret?*/
    mvret GetAddr();   // @0007f410  /*ret?*/
};

// ==== cPaletteFull [rtti] ======================================
class cPaletteFull {
public:
    cPaletteFull();   // @0007f3c0
    cPaletteFull(cPalette *);   // @0007f390
    ~cPaletteFull();   // @0007f330
    mvret GetPalette16();   // @0007ee70  /*ret?*/
    mvret GetPalette();   // @0007f2a0  /*ret?*/
    mvret GetPalette32();   // @0007ecf0  /*ret?*/
    mvret SetPalette(cPalette *);   // @0007f2b0  /*ret?*/
    mvret GetConvert();   // @0007f190  /*ret?*/
    mvret GetPalette15();   // @0007f000  /*ret?*/
};

// ==== cPipe [polymorphic, rtti] ================================
class cPipe {
public:
    cPipe(int, int);   // @00095d50
    ~cPipe();   // @00095d80
    mvret Close();   // @00095db0  /*ret?*/
    mvret CreatePipe(const char *);   // @00095f10  /*ret?*/
    mvret IsBlockMode();   // @00095f00  /*ret?*/
    mvret SetBlockMode(bool);   // @00095ef0  /*ret?*/
    mvret Write(const void *, unsigned long);   // @00095e70  /*ret?*/
    mvret Read(void *, unsigned long);   // @00095dd0  /*ret?*/
    mvret CreatePipe(cPipe * [1] &);   // @00096030  /*ret?*/
    mvret UsePipe(const char *);   // @00095fb0  /*ret?*/
};

// ==== cPointer [polymorphic, rtti] =============================
class cPointer {
public:
    cPointer();   // @000802f0
    ~cPointer();   // @000983b0
    mvret EVENT_Buttons(unsigned char);   // @00080410  /*ret?*/
    mvret EVENT_Move(const tPoint<long> &);   // @00080350  /*ret?*/
    mvret GetNextEvent();   // @000804c0  /*ret?*/
};

// ==== cProcIniFile =============================================
class cProcIniFile {
public:
    mvret AddVariable();   // @00096af0  /*ret?*/
    mvret ReadGroupName();   // @00096c10  /*ret?*/
    mvret Step();   // @00096ca0  /*ret?*/
};

// ==== cProcess [rtti] ==========================================
class cProcess {
public:
    mvret Write(const void *, unsigned long);   // @00098470  /*ret?*/
    mvret IsBlockMode();   // @00098430  /*ret?*/
    mvret SetBlockMode(bool);   // @00098450  /*ret?*/
    mvret Read(void *, unsigned long);   // @000984a0  /*ret?*/
};

// ==== cRandom ==================================================
class cRandom {
public:
    cRandom(unsigned short);   // @000808c0
    cRandom();   // @000808a0
    mvret Next();   // @000808f0  /*ret?*/
    mvret Rnd();   // @000807d0  /*ret?*/
    mvret GetCount();   // @000807c0  /*ret?*/
    mvret Random(unsigned short);   // @000806d0  /*ret?*/
    mvret Init(unsigned short);   // @00080880  /*ret?*/
    mvret Rnd(long, long);   // @00080810  /*ret?*/
    mvret Init();   // @00080860  /*ret?*/
};

// ==== cRectangle [rtti] ========================================
class cRectangle {
public:
};

// ==== cSample [polymorphic, rtti] ==============================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cSample {
public:
    cSample();   // @00080b90
    cSample(cSoundFormat, bool, unsigned long, unsigned long);   // @00080c10
    ~cSample();   // @00080b10
    mvret GetSampleRate();   // @00080970  /*ret?*/
    mvret Construct(cSoundFormat, bool, unsigned long, unsigned long);   // @00080aa0  /*ret?*/
    mvret GetFormat();   // @00080a30  /*ret?*/
    mvret IsStereo();   // @000809d0  /*ret?*/
    mvret GetLength();   // @00080910  /*ret?*/
};

// ==== cScreen ==================================================
class cScreen {
public:
    cScreen(cDimension, eBMType, void (*)(cScreen &) *, void (*)(cScreen &) *);   // @0008f460
    ~cScreen();   // @0008d110
    mvret AddVObject(cVObject *, bool);   // @0008f3d0  /*ret?*/
    mvret Activate();   // @0008f290  /*ret?*/
    mvret GetPalette();   // @0008f260  /*ret?*/
    mvret GetPointer();   // @0008f250  /*ret?*/
    mvret BeginRefresh();   // @0008d2a0  /*ret?*/
    mvret GetNotify();   // @0008f160  /*ret?*/
    mvret Deactivate();   // @0008f270  /*ret?*/
    mvret SetPointer(cSprite *);   // @0008d240  /*ret?*/
    mvret AddRequester(cVObject *, bool);   // @0008f2b0  /*ret?*/
    mvret GetMasterVO();   // @0008f240  /*ret?*/
    mvret EndRefresh();   // @0008d2d0  /*ret?*/
    mvret SetPalette(cPalette &);   // @0008d160  /*ret?*/
};

// ==== cSemaphore [polymorphic, rtti] ===========================
class cSemaphore {
public:
    cSemaphore(unsigned long, bool);   // @00095bd0
    ~cSemaphore();   // @00095c10
    mvret Lock();   // @00095c50  /*ret?*/
    mvret GetVal();   // @00095cf0  /*ret?*/
    mvret TryLock();   // @00095c80  /*ret?*/
    mvret Unlock();   // @00095cc0  /*ret?*/
};

// ==== cSharedData_AnimBitmap [polymorphic, rtti] ===============
class cSharedData_AnimBitmap {
public:
    cSharedData_AnimBitmap(const char *, bool);   // @00050110
    ~cSharedData_AnimBitmap();   // @000501b0
    mvret Delete(cSharedData_AnimBitmap *);   // @00050010  /*ret?*/
    mvret GetAnimBitmap();   // @000500d0  /*ret?*/
    mvret DecUsage();   // @000500e0  /*ret?*/
    mvret IncUsage();   // @00050100  /*ret?*/
    mvret Create(const char *, bool);   // @0004ff00  /*ret?*/
};

// ==== cSharedData_Bitmap [polymorphic, rtti] ===================
class cSharedData_Bitmap {
public:
    cSharedData_Bitmap(const char *);   // @00050490
    ~cSharedData_Bitmap();   // @00050530
    mvret Delete(cSharedData_Bitmap *);   // @00050390  /*ret?*/
    mvret IncUsage();   // @00050480  /*ret?*/
    mvret GetBitmap();   // @00050450  /*ret?*/
    mvret DecUsage();   // @00050460  /*ret?*/
    mvret Create(const char *);   // @00050280  /*ret?*/
};

// ==== cShell [polymorphic, rtti] ===============================
class cShell {
public:
    cShell(const char *);   // @00043130
    ~cShell();   // @00043160
    mvret EchoIsOn() const;   // @00043320  /*ret?*/
    mvret SetConsole(cConsole *);   // @00043310  /*ret?*/
    mvret EchoOff();   // @00043330  /*ret?*/
    mvret GetName() const;   // @00043350  /*ret?*/
    mvret Parser(const char *);   // @00043190  /*ret?*/
    mvret ProcessCommand(int, char **);   // @00043180  /*ret?*/
    mvret EchoOn();   // @00043340  /*ret?*/
};

// ==== cSoundCard [polymorphic, rtti] ===========================
class cSoundCard {
public:
    ~cSoundCard();   // @00081230
};

// ==== cSoundCard_Dummy [polymorphic, rtti] =====================
class cSoundCard_Dummy {
public:
    ~cSoundCard_Dummy();   // @00082710
    mvret GetMasterVolume();   // @000811f0  /*ret?*/
    mvret SetMasterVolume(const cVolume &);   // @00082620  /*ret?*/
    mvret IsStereo();   // @000826b0  /*ret?*/
    mvret StopRecord();   // @00082600  /*ret?*/
    mvret AllocateChannel();   // @00082650  /*ret?*/
    mvret StartRecord(cSoundRecorderBuffer &, unsigned char, unsigned long);   // @00082610  /*ret?*/
    mvret SetProperties(unsigned long, unsigned char, bool);   // @000826a0  /*ret?*/
    mvret GetSoundFormat();   // @000826c0  /*ret?*/
    mvret GetFrequency();   // @000826d0  /*ret?*/
    mvret DeleteChannel(cSoundChannel *);   // @00082630  /*ret?*/
};

// ==== cSoundCard_Linux [polymorphic, rtti] =====================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cThread
class cSoundCard_Linux {
public:
    cSoundCard_Linux(const char *);   // @00092ba0
    ~cSoundCard_Linux();   // @00092de0
    mvret SetSoundFormat(unsigned char);   // @00092ef0  /*ret?*/
    mvret StartRecord(cSoundRecorderBuffer &, unsigned char, unsigned long);   // @00093070  /*ret?*/
    mvret SetFrequency(unsigned long);   // @00092eb0  /*ret?*/
    mvret IsStereo();   // @00092fe0  /*ret?*/
    mvret GetSoundFormat();   // @00092fc0  /*ret?*/
    mvret SetStereo(bool);   // @00092e60  /*ret?*/
    mvret GetFrequency();   // @00092fb0  /*ret?*/
    mvret StopRecord();   // @000930f0  /*ret?*/
    mvret Main();   // @00092b30  /*ret?*/
    mvret SetProperties(unsigned long, unsigned char, bool);   // @00092ff0  /*ret?*/
};

// ==== cSoundCard_SoftwareMix [polymorphic, rtti] ===============
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cThread
class cSoundCard_SoftwareMix {
public:
    cSoundCard_SoftwareMix();   // @00098ac0
    ~cSoundCard_SoftwareMix();   // @000822f0
    mvret SetMasterVolume(const cVolume &);   // @00082460  /*ret?*/
    mvret AllocateChannel();   // @00082390  /*ret?*/
    mvret GetMasterVolume();   // @00082480  /*ret?*/
    mvret DeleteChannel(cSoundChannel *);   // @000823f0  /*ret?*/
};

// ==== cSoundChannel [polymorphic, rtti] ========================
class cSoundChannel {
public:
    cSoundChannel();   // @00083b70
    ~cSoundChannel();   // @00083b40
    mvret Play(cSample &);   // @00083ac0  /*ret?*/
    mvret Notify(eSoundChannelEvent);   // @000839d0  /*ret?*/
    mvret PlayLoop(cSample &);   // @00083a00  /*ret?*/
    mvret SetNotifyObject(cSoundNotify *);   // @00083b30  /*ret?*/
};

// ==== cSoundChannel_SoftwareMix [polymorphic, rtti] ============
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cSoundChannel
class cSoundChannel_SoftwareMix {
public:
    cSoundChannel_SoftwareMix();   // @00082f80
    ~cSoundChannel_SoftwareMix();   // @00083990
    mvret SetSpeed(unsigned short);   // @000838e0  /*ret?*/
    mvret GetDataSLEWSL();   // @00083440  /*ret?*/
    mvret InterpolateRight(short, short);   // @00083610  /*ret?*/
    mvret SetMixSpeed(unsigned short);   // @00083860  /*ret?*/
    mvret GetDataUBMR();   // @00083550  /*ret?*/
    mvret GetDataSLEWM();   // @00083510  /*ret?*/
    mvret GetDataSLEWMR();   // @00083490  /*ret?*/
    mvret IsStereo();   // @00083920  /*ret?*/
    mvret GetFormat();   // @00083900  /*ret?*/
    mvret Play_(cSample &, unsigned long, unsigned long);   // @00082ff0  /*ret?*/
    mvret IsActive();   // @000838a0  /*ret?*/
    mvret InterpolateLeft(short, short);   // @00083650  /*ret?*/
    mvret GetDataUBM();   // @000835d0  /*ret?*/
    mvret GetDataSLEWML();   // @000834d0  /*ret?*/
    mvret GetDataSLEWSR();   // @000833f0  /*ret?*/
    mvret ExitLoop();   // @000838c0  /*ret?*/
    mvret GetDataUBML();   // @00083590  /*ret?*/
    mvret GetPos();   // @000833e0  /*ret?*/
    mvret GetData(unsigned char);   // @000836d0  /*ret?*/
    mvret SetVolume(const cVolume &);   // @000831b0  /*ret?*/
    mvret Step();   // @00083300  /*ret?*/
    mvret Interpolate(short, short);   // @00083690  /*ret?*/
    mvret SetStepSpeed(unsigned long);   // @00083930  /*ret?*/
    mvret Stop();   // @00083240  /*ret?*/
};

// ==== cSoundConvert ============================================
class cSoundConvert {
public:
    cSoundConvert();   // @00083bf0
    mvret SetSource(cSoundFormat, bool, unsigned long, void *, unsigned long);   // @00083c90  /*ret?*/
    mvret Write_Unsupported();   // @00083fb0  /*ret?*/
    mvret Copy();   // @00083e10  /*ret?*/
    mvret Write_SW_Mono();   // @00083f00  /*ret?*/
    mvret Read_Unsupported();   // @00083f90  /*ret?*/
    mvret SetTarget(cSoundFormat, bool, unsigned long, void *, unsigned long);   // @00083d40  /*ret?*/
    mvret Write_UB_Mono();   // @00083f50  /*ret?*/
    mvret Read_SW_Mono();   // @00083ed0  /*ret?*/
};

// ==== cSoundFormat =============================================
class cSoundFormat {
public:
    cSoundFormat(unsigned char);   // @00080e50
    cSoundFormat();   // @00080e70
    mvret operator=(unsigned char);   // @00080e30  /*ret?*/
    mvret __opUc() const;   // @00080e10  /*ret?*/
    mvret GetSize() const;   // @00080df0  /*ret?*/
};

// ==== cSoundNotify [polymorphic, rtti] =========================
class cSoundNotify {
public:
};

// ==== cSoundPlay [polymorphic, rtti] ===========================
class cSoundPlay {
public:
    mvret Notify(eSoundChannelEvent);   // @00098920  /*ret?*/
};

// ==== cSoundRecorderBuffer =====================================
class cSoundRecorderBuffer {
public:
    cSoundRecorderBuffer(cSoundFormat, unsigned char, unsigned short, unsigned long, unsigned char);   // @00081250
    ~cSoundRecorderBuffer();   // @00081370
    mvret Step();   // @00081440  /*ret?*/
    mvret GetBuffer();   // @000813f0  /*ret?*/
    mvret GetFormat();   // @00082800  /*ret?*/
    mvret StepWriteBuffer();   // @00081510  /*ret?*/
    mvret GetChannels();   // @000827e0  /*ret?*/
    mvret Reset();   // @000813c0  /*ret?*/
    mvret GetFrameSize();   // @000827b0  /*ret?*/
    mvret GetWriteBuffer();   // @000814d0  /*ret?*/
    mvret GetFrequency();   // @000827c0  /*ret?*/
    mvret GetPhysicalFrameLength();   // @00082780  /*ret?*/
    mvret Wait();   // @000814a0  /*ret?*/
    mvret GetNuberOfFrames();   // @00082760  /*ret?*/
};

// ==== cSoundRecorderThread [polymorphic, rtti] =================
class cSoundRecorderThread {
public:
    cSoundRecorderThread(int, cSoundRecorderBuffer &);   // @00092990
    ~cSoundRecorderThread();   // @000931c0
    mvret Main();   // @000929c0  /*ret?*/
};

// ==== cSoundServer =============================================
class cSoundServer {
public:
    cSoundServer(unsigned short);   // @00082cb0
    ~cSoundServer();   // @00082c40
    mvret StopAll();   // @00082c00  /*ret?*/
    mvret PlayLoop(cAO &);   // @00082ba0  /*ret?*/
    mvret Play(cAO &);   // @00082b40  /*ret?*/
    mvret GetFreeChannel(long);   // @00082ac0  /*ret?*/
};

// ==== cSoundServerChannel [polymorphic, rtti] ==================
class cSoundServerChannel {
public:
    cSoundServerChannel();   // @00082e00
    ~cSoundServerChannel();   // @00082dc0
    mvret Play(cAO &);   // @00082860  /*ret?*/
    mvret Notify(eSoundChannelEvent);   // @00082a80  /*ret?*/
    mvret GetActualAO();   // @00082db0  /*ret?*/
    mvret Stop();   // @00082a20  /*ret?*/
    mvret PlayLoop(cAO &);   // @00082910  /*ret?*/
    mvret SetVolume(const cVolume &);   // @00082a60  /*ret?*/
};

// ==== cSprABitmapAdd [polymorphic, rtti] =======================
class cSprABitmapAdd {
public:
    cSprABitmapAdd(cAnimBitmap &, cAnimBitmap &);   // @0008bbb0
    ~cSprABitmapAdd();   // @0008c460
    mvret Paint(cGD &, tPoint<long>);   // @0008bf60  /*ret?*/
    mvret Unlock();   // @0008c380  /*ret?*/
    mvret Lock();   // @0008c3b0  /*ret?*/
    mvret Process();   // @0008c2f0  /*ret?*/
};

// ==== cSprClick [polymorphic, rtti] ============================
class cSprClick {
public:
    cSprClick(cAnimBitmap &);   // @0008c710
    ~cSprClick();   // @0008c8a0
    mvret Paint(cGD &, tPoint<long>);   // @0008c530  /*ret?*/
    mvret Process();   // @0008ba70  /*ret?*/
    mvret Init();   // @0008bb20  /*ret?*/
    mvret Unlock();   // @0008c4c0  /*ret?*/
    mvret Lock();   // @0008c4e0  /*ret?*/
};

// ==== cSprite [polymorphic, rtti] ==============================
class cSprite {
public:
    ~cSprite();   // @00098820
    mvret SaveBg(cGD &, cSprite::cBg &);   // @0008b1d0  /*ret?*/
    mvret Process();   // @000987e0  /*ret?*/
    mvret Init();   // @000987d0  /*ret?*/
    mvret RestoreBg(cGD &, cSprite::cBg &);   // @0008b490  /*ret?*/
    mvret Refresh();   // @0008b720  /*ret?*/
    mvret Unlock();   // @000987b0  /*ret?*/
    mvret Lock();   // @000987c0  /*ret?*/
    mvret AfterSwapBuffer();   // @0008b690  /*ret?*/
    mvret BeforeSwapBuffer();   // @0008b650  /*ret?*/
    mvret MoveTo(const tPoint<long> &);   // @0008ba50  /*ret?*/
    mvret Clear();   // @0008b940  /*ret?*/
};

// ==== cStdConv =================================================
class cStdConv {
public:
    cStdConv();   // @0008afb0
    operator unsigned char *();   // @0008afa0
};

// ==== cStream [polymorphic, rtti] ==============================
class cStream {
public:
    mvret GetEventDescriptor();   // @00096800  /*ret?*/
};

// ==== cString [polymorphic, rtti] ==============================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
class cString {
    // is-a tMemBlock<char> -> cMemBlock (0x20). Storage is a lockable/evictable managed block.
public:
    cString(const char *);   // @00083fd0
    ~cString();   // @00084150
    mvret operator<=(const char *);   // @00084630  /*ret?*/
    mvret operator=(const char *);   // @00084210  /*ret?*/
    operator char *();   // @000841e0
    mvret operator+(const char *);   // @00084360  /*ret?*/
    mvret operator>=(const char *);   // @000846f0  /*ret?*/
    mvret operator!=(const char *);   // @00084580  /*ret?*/
    mvret operator<(const char *);   // @000845d0  /*ret?*/
    mvret operator>(const char *);   // @00084690  /*ret?*/
    mvret operator==(const char *);   // @00084530  /*ret?*/
    mvret operator+=(const char *);   // @00084310  /*ret?*/
    mvret GetLength();   // @000841c0  /*ret?*/
};

// ==== cSyncSystem ==============================================
class cSyncSystem {
public:
    mvret CreateEventManager();   // @00095b60  /*ret?*/
    mvret Sleep(unsigned long);   // @00095b50  /*ret?*/
};

// ==== cSystemMemory ============================================
class cSystemMemory {
    // +0x04 tList<cMemBlock>; +0x1c budget=0x2000000 (32MB); +0x20 remaining. Evicts first UNLOCKED block.
public:
    cSystemMemory();   // @0007b090
    mvret Use(cMemBlock &);   // @0007b3b0  /*ret?*/
    mvret FlushAll();   // @0007b0c0  /*ret?*/
    mvret Alloc(cMemBlock &);   // @0007b2b0  /*ret?*/
    mvret Free(cMemBlock &);   // @0007b550  /*ret?*/
    mvret FlushAllUnlocked();   // @0007b170  /*ret?*/
    mvret Dump();   // @0007b3c0  /*ret?*/
};

// ==== cTask [polymorphic, rtti] ================================
class cTask {
public:
    cTask(const char *);   // @00095800
    mvret Wait();   // @000957e0  /*ret?*/
    mvret Stop();   // @000957a0  /*ret?*/
    mvret Kill();   // @000957c0  /*ret?*/
    mvret Launch();   // @00095740  /*ret?*/
};

// ==== cTask_ [polymorphic, rtti] ===============================
class cTask_ {
public:
};

// ==== cTextFile [polymorphic, rtti] ============================
class cTextFile {
public:
    cTextFile(const char *);   // @00055240
    cTextFile(const cFile &);   // @000551d0
    mvret CountLines(int *);   // @00055110  /*ret?*/
    mvret GetPosition();   // @00054e20  /*ret?*/
    mvret ReadLine(char *, int);   // @00055070  /*ret?*/
    mvret OpenR(bool);   // @00054f70  /*ret?*/
    mvret SeekB(long);   // @00054e40  /*ret?*/
    mvret ReadChar(char &);   // @00054d90  /*ret?*/
    mvret Goto(int);   // @00055000  /*ret?*/
    mvret WriteLine(char *, int);   // @000550f0  /*ret?*/
    mvret Read(void *, unsigned long);   // @00054eb0  /*ret?*/
};

// ==== cThread [polymorphic, rtti] ==============================
class cThread {
    // +0x04 cStream vtable base; +0x08/+0x0c pipe fds; +0x10 running; +0x14 pthread_t. Launch=CreatePipe+pthread_create(Entry).
public:
    cThread();   // @00095860
    ~cThread();   // @00095890
    mvret Kill();   // @00095940  /*ret?*/
    mvret Entry(cThread *);   // @00095970  /*ret?*/
    mvret Launch();   // @000958c0  /*ret?*/
    mvret Wait();   // @00095990  /*ret?*/
    mvret Stop();   // @00095920  /*ret?*/
};

// ==== cThread_ [rtti] ==========================================
class cThread_ {
public:
};

// ==== cTimerSystem [polymorphic, rtti] =========================
class cTimerSystem {
public:
    ~cTimerSystem();   // @000989b0
};

// ==== cTimerSystem_Linux [polymorphic, rtti] ===================
class cTimerSystem_Linux {
public:
    cTimerSystem_Linux();   // @00092430
    ~cTimerSystem_Linux();   // @000924f0
    mvret _ActivateTimer(cVTimer &);   // @00092590  /*ret?*/
    mvret _DeactivateTimer(cVTimer &);   // @000925c0  /*ret?*/
    mvret Proc();   // @000925f0  /*ret?*/
};

// ==== cVCD [polymorphic, rtti] =================================
class cVCD {
public:
    cVCD();   // @00080680
    ~cVCD();   // @00080660
    mvret PlayAll();   // @000805f0  /*ret?*/
    mvret Play(unsigned long, unsigned long);   // @00080580  /*ret?*/
    mvret GetNumberOfTracks();   // @00080640  /*ret?*/
    mvret Play(unsigned long);   // @000805b0  /*ret?*/
};

// ==== cVMode ===================================================
class cVMode {
public:
    cVMode(cDimension, eBMType);   // @0008b140
    mvret operator==(const cVMode &);   // @0008b0f0  /*ret?*/
    mvret SetBMType(eBMType);   // @0008b120  /*ret?*/
    mvret GetBMType() const;   // @0008b130  /*ret?*/
};

// ==== cVModeRequest ============================================
class cVModeRequest {
public:
    cVModeRequest(cDimension, eBMType);   // @0008b0c0
    mvret SetEmulationMode(eVModeEmu);   // @0008b0b0  /*ret?*/
    mvret SetBufferTech(eBufferTech);   // @0008b0a0  /*ret?*/
    mvret GetBufferTech() const;   // @0008b090  /*ret?*/
};

// ==== cVOAButton [polymorphic, rtti] ===========================
class cVOAButton {
public:
    cVOAButton(const cRectangle &, const cColor &, cFont &, const char *, eVOAButtonTextAlignment, bool, bool, bool, unsigned short, cVObject *);   // @00045910
    cVOAButton(const cRectangle &, cBitmap &, cFont &, const char *, eVOAButtonTextAlignment, bool, bool, bool, unsigned short, cVObject *);   // @00045310
    cVOAButton(const tPoint<long> &, cBitmap &, cBitmap &, bool, bool, bool, unsigned short, cVObject *);   // @00044fb0
    cVOAButton(const cRectangle &, cFont &, const char *, eVOAButtonTextAlignment, bool, bool, bool, unsigned short, cVObject *);   // @00045610
    cVOAButton(const tPoint<long> &, cBitmap &, cBitmap &, cFont &, const char *, eVOAButtonTextAlignment, const tPoint<long> &, bool, bool, bool, unsigned short, cVObject *);   // @00045c10
    ~cVOAButton();   // @00045f70
    mvret SetHotkey(eKeyCode, unsigned char);   // @00047900  /*ret?*/
    mvret SetText(const char *);   // @00047870  /*ret?*/
    mvret PaintText(cGD &, const tPoint<long> &, const cRectangle &);   // @000465d0  /*ret?*/
    mvret CreateDPal();   // @000462e0  /*ret?*/
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @00046710  /*ret?*/
    mvret SetSoundSrv(cSoundServer &);   // @000464e0  /*ret?*/
    mvret IsDisabled();   // @00047800  /*ret?*/
    mvret SetRepeatValues(unsigned long, unsigned long, unsigned long, unsigned long);   // @000477b0  /*ret?*/
    mvret Process(sInput &);   // @000472f0  /*ret?*/
    mvret SetTextOffset(const tPoint<long> &);   // @00047920  /*ret?*/
    mvret Initialize();   // @00047720  /*ret?*/
    mvret SetRepeatMode(bool);   // @00047790  /*ret?*/
    mvret SetBitmapIsAMask(bool);   // @00047980  /*ret?*/
    mvret SetFrameSize(unsigned long);   // @00047940  /*ret?*/
    mvret IsPressed();   // @00047860  /*ret?*/
    mvret GetText();   // @000478f0  /*ret?*/
    mvret SetDisableColor(const cColor &);   // @00047960  /*ret?*/
    mvret CreateBPal();   // @000460e0  /*ret?*/
    mvret Release();   // @000465b0  /*ret?*/
    mvret Enable();   // @00047810  /*ret?*/
    mvret SetText(cFont *, const char *);   // @000478d0  /*ret?*/
    mvret Disable();   // @00047830  /*ret?*/
    mvret SetSample(cSample &);   // @00046500  /*ret?*/
    mvret Press();   // @00046570  /*ret?*/
};

// ==== cVOBitmap [polymorphic, rtti] ============================
class cVOBitmap {
public:
    cVOBitmap(const tPoint<long> &, cBitmap &, bool, unsigned short, cVObject *);   // @0005b360
    ~cVOBitmap();   // @0005b540
    mvret Process(sInput &);   // @0005afd0  /*ret?*/
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @0005b070  /*ret?*/
};

// ==== cVOButton [polymorphic, rtti] ============================
class cVOButton {
public:
    ~cVOButton();   // @00097140
};

// ==== cVOConsole [polymorphic, rtti] ===========================
class cVOConsole {
public:
    cVOConsole(int);   // @00043390
    ~cVOConsole();   // @000433c0
    mvret Edit();   // @00043670  /*ret?*/
    mvret Hide();   // @00043810  /*ret?*/
    mvret Show();   // @00043520  /*ret?*/
    mvret IsVisible();   // @00044f60  /*ret?*/
    mvret Setup(cIntuition *, const cRectangle &, cData_Font &, bool);   // @00043400  /*ret?*/
    mvret SetExitKey(eKeyCode, unsigned char);   // @000434d0  /*ret?*/
    mvret GetVO();   // @00044f50  /*ret?*/
};

// ==== cVODragBox [polymorphic, rtti] ===========================
class cVODragBox {
public:
    ~cVODragBox();   // @00098330
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @000980f0  /*ret?*/
    mvret Initialize();   // @00097eb0  /*ret?*/
    mvret Bound();   // @00097ce0  /*ret?*/
    mvret GetYY();   // @00097e30  /*ret?*/
    mvret GetXX();   // @00097e70  /*ret?*/
    mvret Process(sInput &);   // @00097ed0  /*ret?*/
};

// ==== cVOEditRow [polymorphic, rtti] ===========================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cEditRow
class cVOEditRow {
public:
    cVOEditRow(const cRectangle &, cData_Font &, cData_Bitmap *, unsigned short, unsigned long, unsigned short, cVObject *);   // @00052500
    ~cVOEditRow();   // @000527d0
    mvret operator=(const char *);   // @000516f0  /*ret?*/
    operator char *();   // @000524e0
    mvret SetStatus(eER_Status);   // @00052490  /*ret?*/
    mvret GetFontOffset();   // @00052420  /*ret?*/
    mvret ShowCursor(cGD &, const tPoint<long> &, const cRectangle &);   // @000514f0  /*ret?*/
    mvret Key(eKeyCode, bool);   // @00051190  /*ret?*/
    mvret SetOptions(unsigned short);   // @00052440  /*ret?*/
    mvret Process(sInput &);   // @000512a0  /*ret?*/
    mvret SetFontOffset(short);   // @00052400  /*ret?*/
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @00051750  /*ret?*/
    mvret GetOptions();   // @00052460  /*ret?*/
    mvret ChkCh(char);   // @000522b0  /*ret?*/
    mvret SetCursorPos(const tPoint<long> &);   // @000513b0  /*ret?*/
    mvret GetPoiFontOffset();   // @000523e0  /*ret?*/
    mvret MouseEvent(tPoint<long>);   // @000510e0  /*ret?*/
    mvret Empty();   // @000524b0  /*ret?*/
    mvret Raw2Char(eKeyCode, bool);   // @00050b40  /*ret?*/
    mvret GetStatus();   // @00052480  /*ret?*/
    mvret Deletion();   // @00052270  /*ret?*/
    mvret SetPoiFontOffset(tPoint<long>);   // @000523c0  /*ret?*/
    mvret FontWidth(char);   // @00052340  /*ret?*/
};

// ==== cVOFGraphs ===============================================
class cVOFGraphs {
public:
    cVOFGraphs(cRectangle, cData_Bitmap &, cVOButton &, cVOButton &, cVOMultiLine &, cVOEditRow &, cVOEditRow &);   // @00054830
};

// ==== cVOFiler [polymorphic, rtti] =============================
class cVOFiler {
public:
    cVOFiler(eFilerType, const char *, const cVOFGraphs &, taList<cFFilter> *, unsigned short, cVObject *);   // @000541d0
    ~cVOFiler();   // @00054760
    mvret HandleCancel();   // @00053f10  /*ret?*/
    mvret Select(cDirent &);   // @00053c70  /*ret?*/
    mvret HandleOk();   // @00053e60  /*ret?*/
    mvret Request(cScreen &);   // @00053990  /*ret?*/
    mvret Fill();   // @000534c0  /*ret?*/
    mvret Notify(const sVOMessage &);   // @00053f30  /*ret?*/
    mvret SetEmpty();   // @00053460  /*ret?*/
    mvret Process(sInput &);   // @00054030  /*ret?*/
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @00054040  /*ret?*/
    mvret GetAct();   // @00053790  /*ret?*/
    mvret HandleMultiLn();   // @00053cf0  /*ret?*/
};

// ==== cVOListReq [polymorphic, rtti] ===========================
class cVOListReq {
public:
    cVOListReq(const cRectangle &, unsigned long, cBitmap &, cFont &, const char **);   // @00048200
    cVOListReq(const tPoint<long> &, cBitmap &, cFont &, const char **);   // @00048020
    cVOListReq(const tPoint<long> &, cBitmap &, cFont &, const char *, ...);   // @00047cc0
    cVOListReq(const cRectangle &, cBitmap &, cFont &, const char **);   // @00047ea0
    cVOListReq(const cRectangle &, cBitmap &, cFont &, const char *, ...);   // @00047b40
    cVOListReq(const tPoint<long> &, unsigned long, cBitmap &, cBitmap &, cFont &, const char **);   // @00048380
    ~cVOListReq();   // @00048d90
    mvret Disable(unsigned long);   // @00048f80  /*ret?*/
    mvret SetSoundSrv(cSoundServer &);   // @00048e30  /*ret?*/
    mvret Notify(const sVOMessage &);   // @00048f20  /*ret?*/
    mvret Enable(unsigned long);   // @00048fe0  /*ret?*/
    mvret Const2(unsigned long, cBitmap &, cFont &, const char **);   // @00048780  /*ret?*/
    mvret Process(sInput &);   // @00048eb0  /*ret?*/
    mvret Const3(unsigned long, cBitmap &, cBitmap &, cFont &, const char **);   // @00048a80  /*ret?*/
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @0004a280  /*ret?*/
    mvret Const(const cRectangle &, unsigned long, cBitmap &, cFont &, const char **);   // @00048560  /*ret?*/
    mvret SetSample(cSample &);   // @00048e70  /*ret?*/
};

// ==== cVOMsgBox [polymorphic, rtti] ============================
class cVOMsgBox {
public:
    cVOMsgBox(const tPoint<long> &, char *, cFont &, cBitmap &, cBitmap &, cBitmap &);   // @0005a1b0
    ~cVOMsgBox();   // @0005a130
    mvret Notify(const sVOMessage &);   // @00059e70  /*ret?*/
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @00059ee0  /*ret?*/
    mvret Process(sInput &);   // @00059ec0  /*ret?*/
};

// ==== cVOMultiLine [polymorphic, rtti] =========================
class cVOMultiLine {
public:
    cVOMultiLine(const cRectangle &, cData_Font &, cData_Bitmap &, unsigned short, cVObject *);   // @0007c7a0
    cVOMultiLine(const cRectangle &, cData_Font &, cData_Palette &, cData_Bitmap &, unsigned short, cVObject *);   // @0007c0f0
    ~cVOMultiLine();   // @0007ce60
    operator char *();   // @0007e6c0
    mvret ClearEntryString(unsigned char);   // @0007e600  /*ret?*/
    mvret InsEndItem(const char *);   // @0007e710  /*ret?*/
    mvret Process(sInput &);   // @0007d370  /*ret?*/
    mvret InsertItem(unsigned short, const char *);   // @0007d6b0  /*ret?*/
    mvret SetEmpty();   // @0007dd10  /*ret?*/
    mvret SetPoiFontOffset(tPoint<long>);   // @0007e680  /*ret?*/
    mvret Paint(bool);   // @0007d050  /*ret?*/
    mvret Key(eKeyCode);   // @0007d620  /*ret?*/
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @0007d320  /*ret?*/
    mvret SetEntryString(unsigned char, char *);   // @0007e640  /*ret?*/
    mvret Notify(const sVOMessage &);   // @0007d3c0  /*ret?*/
    mvret DisappearSlider();   // @0007e490  /*ret?*/
    mvret ReBuild();   // @0007cfa0  /*ret?*/
    mvret RemoveItem(unsigned short);   // @0007da20  /*ret?*/
    mvret GetCount();   // @0007e6f0  /*ret?*/
    mvret Action(cVOMultiLine::eML_Action);   // @0007d510  /*ret?*/
    mvret AppearSlider();   // @0007ddd0  /*ret?*/
};

// ==== cVOPulldown [polymorphic, rtti] ==========================
class cVOPulldown {
public:
    cVOPulldown(const cRectangle &, cBitmap &, cFont &, const char *, ...);   // @00049030
    cVOPulldown(const tPoint<long> &, cBitmap &, cBitmap &, cBitmap &, cBitmap &, const tPoint<long> &, cFont &, const char *, ...);   // @000497c0
    cVOPulldown(const tPoint<long> &, cBitmap &, cBitmap &, cBitmap &, cBitmap &, cFont &, const char *, ...);   // @00049300
    ~cVOPulldown();   // @0004a1f0
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @00049d60  /*ret?*/
    mvret Process(sInput &);   // @0004a190  /*ret?*/
    mvret Enable(unsigned long);   // @0004a150  /*ret?*/
    mvret Disable(unsigned long);   // @0004a0f0  /*ret?*/
    mvret GetAct();   // @0004a1a0  /*ret?*/
    mvret Notify(const sVOMessage &);   // @00049fa0  /*ret?*/
    mvret SetHelpText(const char *);   // @0004a170  /*ret?*/
    mvret SetAct(unsigned long);   // @00049c90  /*ret?*/
};

// ==== cVOSliderV [polymorphic, rtti] ===========================
class cVOSliderV {
public:
    ~cVOSliderV();   // @00097c20
    mvret Notify(const sVOMessage &);   // @00097660  /*ret?*/
    mvret SetMinMax(long, long);   // @00097990  /*ret?*/
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @00097730  /*ret?*/
    mvret Process(sInput &);   // @00097720  /*ret?*/
};

// ==== cVOTextBox [polymorphic, rtti] ===========================
class cVOTextBox {
public:
    cVOTextBox(const cRectangle &, char *, cFont *, unsigned short, cVObject *);   // @0005ae10
    ~cVOTextBox();   // @0005af60
    mvret GetWordLength(char *);   // @0005aac0  /*ret?*/
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @0005abf0  /*ret?*/
    mvret PutWord(cGD &, char *, tPoint<long>);   // @0005a790  /*ret?*/
    mvret Process(sInput &);   // @0005a6f0  /*ret?*/
};

// ==== cVOWindow [polymorphic, rtti] ============================
class cVOWindow {
public:
    cVOWindow(const tPoint<long> &, cBitmap &, cRectangle, unsigned short, cVObject *);   // @00059b90
    ~cVOWindow();   // @00059e00
    mvret Paint(cGD &, const tPoint<long> &, const cRectangle &);   // @000599f0  /*ret?*/
    mvret Process(sInput &);   // @000597d0  /*ret?*/
};

// ==== cVObject [polymorphic, rtti] =============================
class cVObject {
public:
    cVObject(const cRectangle &, unsigned short, cVObject *, unsigned short);   // @000900c0
    ~cVObject();   // @000901d0
    mvret AddRequester(cVObject *, bool);   // @0008fd60  /*ret?*/
    mvret RefreshDone();   // @0008fff0  /*ret?*/
    mvret NeedRefresh();   // @00090010  /*ret?*/
    mvret Remove();   // @0008fba0  /*ret?*/
    mvret Resize(const cDimension &);   // @0008fbc0  /*ret?*/
    mvret IsGottaRemove();   // @0008fbb0  /*ret?*/
    mvret Initialize();   // @0008ffc0  /*ret?*/
    mvret GetMirrorGD();   // @00090030  /*ret?*/
    mvret SendMsg(unsigned long);   // @0008ff60  /*ret?*/
    mvret SetReq(bool);   // @0008fb40  /*ret?*/
    mvret InitTree();   // @0008cc00  /*ret?*/
    mvret GetVOinFocusTree();   // @0008cd80  /*ret?*/
    mvret Notify(const sVOMessage &);   // @0008ffd0  /*ret?*/
    mvret PaintTree(cGD &);   // @0008caf0  /*ret?*/
    mvret RemoveTree();   // @0008cdd0  /*ret?*/
    mvret SetHelpText(const char *);   // @0008fb50  /*ret?*/
    mvret GetHelpText();   // @0008fb60  /*ret?*/
    mvret RefreshTree();   // @0008cab0  /*ret?*/
    mvret Refresh();   // @00090000  /*ret?*/
    mvret IsInFocus();   // @0008ced0  /*ret?*/
    mvret HasMirror();   // @00090040  /*ret?*/
    mvret SelfDest();   // @0008fb90  /*ret?*/
    mvret SetOutput(cVObject *);   // @0008fd50  /*ret?*/
    mvret MoveTo(tPoint<long>, bool);   // @0008fc70  /*ret?*/
    mvret ForwardMsg(const sVOMessage &);   // @0008ff20  /*ret?*/
    mvret CalcAbsCoordTree(tPoint<long>, cRectangle);   // @0008cc40  /*ret?*/
    mvret ProcessTree(sInput &);   // @0008cb80  /*ret?*/
    mvret IsReq();   // @0008fb30  /*ret?*/
    mvret DisplayMirror(cGD &, const tPoint<long> &);   // @0008c900  /*ret?*/
    mvret MoveToCenter(cVObject *);   // @0008cf40  /*ret?*/
    mvret AddChild(cVObject *);   // @0008fe90  /*ret?*/
    mvret RemoveR();   // @0008d0b0  /*ret?*/
    mvret SelfDestR();   // @0008d0e0  /*ret?*/
    mvret GetRelPointerPos();   // @0008ce90  /*ret?*/
    mvret CreateMirror(eBMType);   // @00090060  /*ret?*/
    mvret AddChildL(cVObject *);   // @0008fe00  /*ret?*/
};

// ==== cVTimer [polymorphic, rtti] ==============================
class cVTimer {
public:
    ~cVTimer();   // @00096750
    mvret Deactivate();   // @000966f0  /*ret?*/
};

// ==== cVVC [polymorphic, rtti] =================================
class cVVC {
public:
    cVVC();   // @00087ad0
    ~cVVC();   // @00087aa0
    mvret SwapBuffers();   // @00085e20  /*ret?*/
    mvret OpenDisplay(const cVModeRequest &);   // @00085ce0  /*ret?*/
    mvret SetPalette(const cColor *, unsigned short, unsigned short);   // @000877c0  /*ret?*/
    mvret GetWorkBuffer() const;   // @00087760  /*ret?*/
    mvret GetSetPageFunction();   // @000879d0  /*ret?*/
    mvret DoubleBuffering() const;   // @00087720  /*ret?*/
    mvret GetBMType() const;   // @00087710  /*ret?*/
    mvret SetPalette(cPalette &);   // @00087900  /*ret?*/
    mvret GetWorkGD() const;   // @00087740  /*ret?*/
    mvret GetDisplayBuffer() const;   // @00087780  /*ret?*/
    mvret SetPage(unsigned short);   // @000879e0  /*ret?*/
    mvret GetBuffer1GD();   // @00087a60  /*ret?*/
    mvret SetBuffers();   // @00085c90  /*ret?*/
    mvret DBSupport(cDimension, eBMType) const;   // @000877b0  /*ret?*/
    mvret GetSize() const;   // @00087700  /*ret?*/
    mvret DBSupport() const;   // @000877a0  /*ret?*/
    mvret Clear();   // @00087a00  /*ret?*/
    mvret GetDisplayGD() const;   // @00087750  /*ret?*/
    mvret LFB();   // @00087a80  /*ret?*/
    mvret GetBuffer0GD();   // @00087a70  /*ret?*/
};

// ==== cVolume ==================================================
class cVolume {
public:
    cVolume(short);   // @00090470
    cVolume(short, short);   // @00090490
    mvret Set(short, short);   // @00090450  /*ret?*/
    mvret GetRight() const;   // @000903c0  /*ret?*/
    mvret Set(short);   // @00090430  /*ret?*/
    mvret SetRight(short);   // @00090400  /*ret?*/
    mvret SetLeft(short);   // @00090420  /*ret?*/
    mvret Get() const;   // @000903a0  /*ret?*/
    mvret GetLeft() const;   // @000903e0  /*ret?*/
};

// ==== cVolumeHP ================================================
class cVolumeHP {
public:
    cVolumeHP(short);   // @00090380
};

// ==== sBMPHeader::sBMPInfoHeader ===============================
struct sBMPHeader::sBMPInfoHeader {
public:
    sBMPInfoHeader(const cDimension &);   // @0004c600
    mvret GetBMType();   // @0004c660  /*ret?*/
};

// ==== sFLC_Chunk ===============================================
struct sFLC_Chunk {
public:
    mvret GetNext();   // @00057510  /*ret?*/
    mvret GetNumberOfSubChunks();   // @000574f0  /*ret?*/
};

// ==== sFLC_Frame ===============================================
struct sFLC_Frame {
public:
    mvret GetFirst();   // @00057310  /*ret?*/
    mvret GetNext();   // @00057300  /*ret?*/
    mvret Show(cGD &, cPalette &, const cRectangle &);   // @00057020  /*ret?*/
};

// ==== sFLC_FrameBlack ==========================================
struct sFLC_FrameBlack {
public:
    mvret Paint(cGD &, const cRectangle &);   // @00057470  /*ret?*/
};

// ==== sFLC_FrameByteRun ========================================
struct sFLC_FrameByteRun {
public:
    mvret Paint(cGD &, const cRectangle &);   // @00055540  /*ret?*/
    mvret GetDataAddr();   // @00057420  /*ret?*/
    mvret GetDataSize();   // @00057410  /*ret?*/
};

// ==== sFLC_FrameColor ==========================================
struct sFLC_FrameColor {
public:
    mvret GetDataSize();   // @00057380  /*ret?*/
    mvret ChangePalette(cPalette &);   // @000557f0  /*ret?*/
    mvret GetDataAddr();   // @00057390  /*ret?*/
};

// ==== sFLC_FrameColor256 =======================================
struct sFLC_FrameColor256 {
public:
    mvret GetDataAddr();   // @00057360  /*ret?*/
    mvret ChangePalette(cPalette &);   // @000558d0  /*ret?*/
    mvret GetDataSize();   // @00057350  /*ret?*/
};

// ==== sFLC_FrameLC =============================================
struct sFLC_FrameLC {
public:
    mvret Paint(cGD &, const cRectangle &);   // @00055720  /*ret?*/
    mvret GetDataSize();   // @000573b0  /*ret?*/
    mvret GetDataAddr();   // @000573c0  /*ret?*/
};

// ==== sFLC_FrameSS2 ============================================
struct sFLC_FrameSS2 {
public:
    mvret GetDataAddr();   // @000573f0  /*ret?*/
    mvret GetDataSize();   // @000573e0  /*ret?*/
    mvret Paint(cGD &, const cRectangle &);   // @00055600  /*ret?*/
};

// ==== sFLC_FrameSound ==========================================
struct sFLC_FrameSound {
public:
    mvret GetDataSize();   // @00057320  /*ret?*/
    mvret GetDataAddr();   // @00057330  /*ret?*/
    mvret Play();   // @000559b0  /*ret?*/
};

// ==== sFLC_FrameUncompressed ===================================
struct sFLC_FrameUncompressed {
public:
    mvret GetDataAddr();   // @00057450  /*ret?*/
    mvret GetDataSize();   // @00057440  /*ret?*/
    mvret Paint(cGD &, const cRectangle &);   // @000554c0  /*ret?*/
};

// ==== sFLC_Header ==============================================
struct sFLC_Header {
public:
    mvret GetDimension();   // @00056fe0  /*ret?*/
    mvret Play(cGD &, const tPoint<long> &);   // @00056970  /*ret?*/
    mvret GetWidth();   // @00056f60  /*ret?*/
    mvret GetFirstChunk();   // @00057010  /*ret?*/
    mvret GetFrames();   // @00056f80  /*ret?*/
    mvret IsValid();   // @00056fb0  /*ret?*/
    mvret GetHeight();   // @00056f40  /*ret?*/
    mvret GetSize();   // @00056fa0  /*ret?*/
};

// ==== sFLC_Prefix ==============================================
struct sFLC_Prefix {
public:
    mvret GetNext();   // @000574b0  /*ret?*/
    mvret IsValid();   // @000574d0  /*ret?*/
    mvret GetFirst();   // @000574c0  /*ret?*/
};

// ==== sFLC_SubChunk ============================================
struct sFLC_SubChunk {
public:
    mvret GetNext();   // @00057550  /*ret?*/
    mvret GetSize();   // @00057520  /*ret?*/
    mvret GetType();   // @00057530  /*ret?*/
    mvret IsPrefix();   // @00057580  /*ret?*/
    mvret IsFrame();   // @00057560  /*ret?*/
};

// ==== sInput ===================================================
struct sInput {
public:
    sInput(eIType, eMButt);   // @000902d0
    sInput(eIType, const tPoint<long> &);   // @000902f0
    sInput();   // @00090340
    sInput(eIType, eKeyCode);   // @000902b0
    sInput(eIType);   // @00090320
};

// ==== sMCoord ==================================================
struct sMCoord {
public:
    operator tPoint<long>();   // @00090350
};

// ==== sMVOSANIMHeader ==========================================
struct sMVOSANIMHeader {
public:
    mvret Filter();   // @000969b0  /*ret?*/
};

// ==== sRawPicHeader ============================================
struct sRawPicHeader {
public:
    mvret GetNofColors() const;   // @000969e0  /*ret?*/
};

// ==== sRiff ====================================================
struct sRiff {
public:
    mvret Filter();   // @000904b0  /*ret?*/
    mvret GetData();   // @000909f0  /*ret?*/
};

// ==== sSPR1 ====================================================
struct sSPR1 {
public:
    mvret Filter();   // @00096950  /*ret?*/
    mvret Filter3();   // @000968f0  /*ret?*/
    mvret MakePalette(cPalette &, unsigned long);   // @0004df90  /*ret?*/
    mvret MakeAnimBitmap2(cAnimBitmap &, unsigned long);   // @0004dd90  /*ret?*/
    mvret MakeAnimBitmap3(cAnimBitmap &, unsigned long);   // @0004de90  /*ret?*/
    mvret MakeAnimBitmap(cAnimBitmap &, unsigned long);   // @0004dc90  /*ret?*/
    mvret Filter2();   // @00096920  /*ret?*/
};

// ==== sTER1 ====================================================
struct sTER1 {
public:
    mvret MakeAnimBitmap(cAnimBitmap &, unsigned long);   // @0004e140  /*ret?*/
    mvret Filter();   // @000968c0  /*ret?*/
};

// ==== sVModeInfo ===============================================
struct sVModeInfo {
public:
    sVModeInfo(const cDimension &, eBMType);   // @0008b190
    mvret GetWidth();   // @0008b180  /*ret?*/
    mvret GetHeight();   // @0008b170  /*ret?*/
};

// ==== sVOMessage ===============================================
struct sVOMessage {
public:
    sVOMessage(cVObject *, unsigned long);   // @00090280
    sVOMessage();   // @000902a0
};

// ==== sWave ====================================================
struct sWave {
public:
    mvret GetSampleLength();   // @000907a0  /*ret?*/
    mvret MakeSample(cSample &);   // @000905a0  /*ret?*/
    mvret GetSampleAddr();   // @00090790  /*ret?*/
    mvret GetSampleRate();   // @00090780  /*ret?*/
    mvret IsStereo();   // @00090700  /*ret?*/
    mvret Info();   // @000907f0  /*ret?*/
    mvret Filter();   // @00090540  /*ret?*/
    mvret GetSampleFormat();   // @00090730  /*ret?*/
};

// ==== sWave::sData =============================================
struct sWave::sData {
public:
    mvret GetLength();   // @000908a0  /*ret?*/
    mvret Filter();   // @00090510  /*ret?*/
    mvret Info();   // @000908c0  /*ret?*/
    mvret GetAddr();   // @000908b0  /*ret?*/
};

// ==== sWave::sFormat ===========================================
struct sWave::sFormat {
public:
    mvret GetFormat();   // @000908e0  /*ret?*/
    mvret IsStereo();   // @00090930  /*ret?*/
    mvret Info();   // @00090970  /*ret?*/
    mvret Filter();   // @000904e0  /*ret?*/
    mvret GetSampleRate();   // @00090960  /*ret?*/
};

// ============ TEMPLATE INSTANTIATIONS (32) ============
// (commented — the underlying template definitions are not recovered)

// ==== cArray<char *> ===========================================
// template instantiation — body commented (needs the primary template def)
// class cArray<char *> {
// public:
//     cArray(int);   // @000974d0
//     ~cArray();   // @00097500
//     mvret operator[](int);   // @00096e20  /*ret?*/
//     mvret IncrementTo(int);   // @00096d80  /*ret?*/
//     mvret SetEmpty();   // @00097560  /*ret?*/
//     mvret IsValid(int);   // @00097580  /*ret?*/
//     mvret InsertPos(int);   // @000975b0  /*ret?*/
//     mvret RemovePos(int);   // @00097600  /*ret?*/
// };

// ==== tHNode<cVObject> [polymorphic, rtti] =====================
// template instantiation — body commented (needs the primary template def)
// class tHNode<cVObject> {
// public:
//     tHNode();   // @00096290
//     ~tHNode();   // @000965b0
// };

// ==== tList<cDirent> ===========================================
// template instantiation — body commented (needs the primary template def)
// class tList<cDirent> {
// public:
//     tList();   // @00096f60
// };

// ==== tList<cEnvClass> =========================================
// template instantiation — body commented (needs the primary template def)
// class tList<cEnvClass> {
// public:
//     tList();   // @00096cc0
// };

// ==== tList<cEnvVar> ===========================================
// template instantiation — body commented (needs the primary template def)
// class tList<cEnvVar> {
// public:
//     tList();   // @00096cf0
// };

// ==== tList<cIPCSession_IPX> ===================================
// template instantiation — body commented (needs the primary template def)
// class tList<cIPCSession_IPX> {
// public:
//     tList();   // @000972c0
// };

// ==== tList<cLocaleEntry> ======================================
// template instantiation — body commented (needs the primary template def)
// class tList<cLocaleEntry> {
// public:
//     tList();   // @000971c0
// };

// ==== tList<cMemBlock> =========================================
// template instantiation — body commented (needs the primary template def)
// class tList<cMemBlock> {
// public:
//     tList();   // @00097400
// };

// ==== tList<cMsgRecNode> =======================================
// template instantiation — body commented (needs the primary template def)
// class tList<cMsgRecNode> {
// public:
//     tList();   // @00098720
// };

// ==== tList<cMsgTypeNode> ======================================
// template instantiation — body commented (needs the primary template def)
// class tList<cMsgTypeNode> {
// public:
//     tList();   // @000986f0
// };

// ==== tList<cSharedData_AnimBitmap> ============================
// template instantiation — body commented (needs the primary template def)
// class tList<cSharedData_AnimBitmap> {
// public:
//     tList();   // @00096a00
// };

// ==== tList<cSharedData_Bitmap> ================================
// template instantiation — body commented (needs the primary template def)
// class tList<cSharedData_Bitmap> {
// public:
//     tList();   // @00096a60
// };

// ==== tList<cSoundChannel_SoftwareMix> =========================
// template instantiation — body commented (needs the primary template def)
// class tList<cSoundChannel_SoftwareMix> {
// public:
//     tList();   // @00098b40
// };

// ==== tList<cVTimer> ===========================================
// template instantiation — body commented (needs the primary template def)
// class tList<cVTimer> {
// public:
//     tList();   // @00098a70
// };

// ==== tMemBlock<cColor> [polymorphic, rtti] ====================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
// template instantiation — body commented (needs the primary template def)
// class tMemBlock<cColor> {
// public:
//     ~tMemBlock();   // @00096660
// };

// ==== tMemBlock<char> [polymorphic, rtti] ======================
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
// template instantiation — body commented (needs the primary template def)
// class tMemBlock<char> {
// public:
//     tMemBlock(const tMemBlock<char> &);   // @000985f0
//     ~tMemBlock();   // @00098690
// };

// ==== tMemBlock<unsigned char> [polymorphic, rtti] =============
//   bases (vtable-mixin hints, unordered/possibly-incomplete): cMemBlock_
// template instantiation — body commented (needs the primary template def)
// class tMemBlock<unsigned char> {
// public:
//     ~tMemBlock();   // @00096440
// };

// ==== tNode<cDirent> [rtti] ====================================
// template instantiation — body commented (needs the primary template def)
// class tNode<cDirent> {
// public:
// };

// ==== tNode<cEnvClass> [rtti] ==================================
// template instantiation — body commented (needs the primary template def)
// class tNode<cEnvClass> {
// public:
// };

// ==== tNode<cEnvVar> [rtti] ====================================
// template instantiation — body commented (needs the primary template def)
// class tNode<cEnvVar> {
// public:
// };

// ==== tNode<cFFilter> [rtti] ===================================
// template instantiation — body commented (needs the primary template def)
// class tNode<cFFilter> {
// public:
// };

// ==== tNode<cIPCSession_IPX> [polymorphic, rtti] ===============
// template instantiation — body commented (needs the primary template def)
// class tNode<cIPCSession_IPX> {
// public:
//     ~tNode();   // @00097290
// };

// ==== tNode<cLocaleEntry> [rtti] ===============================
// template instantiation — body commented (needs the primary template def)
// class tNode<cLocaleEntry> {
// public:
// };

// ==== tNode<cMemBlock> [polymorphic, rtti] =====================
// template instantiation — body commented (needs the primary template def)
// class tNode<cMemBlock> {
// public:
//     ~tNode();   // @000964c0
// };

// ==== tNode<cMsgRecNode> [rtti] ================================
// template instantiation — body commented (needs the primary template def)
// class tNode<cMsgRecNode> {
// public:
// };

// ==== tNode<cMsgTypeNode> [rtti] ===============================
// template instantiation — body commented (needs the primary template def)
// class tNode<cMsgTypeNode> {
// public:
// };

// ==== tNode<cSharedData_AnimBitmap> [rtti] =====================
// template instantiation — body commented (needs the primary template def)
// class tNode<cSharedData_AnimBitmap> {
// public:
// };

// ==== tNode<cSharedData_Bitmap> [rtti] =========================
// template instantiation — body commented (needs the primary template def)
// class tNode<cSharedData_Bitmap> {
// public:
// };

// ==== tNode<cSoundChannel_SoftwareMix> [polymorphic, rtti] =====
// template instantiation — body commented (needs the primary template def)
// class tNode<cSoundChannel_SoftwareMix> {
// public:
//     ~tNode();   // @00098550
// };

// ==== tNode<cSprite> [rtti] ====================================
// template instantiation — body commented (needs the primary template def)
// class tNode<cSprite> {
// public:
// };

// ==== tNode<cVTimer> [rtti] ====================================
// template instantiation — body commented (needs the primary template def)
// class tNode<cVTimer> {
// public:
// };

// ==== tPoint<long> [rtti] ======================================
// template instantiation — body commented (needs the primary template def)
// class tPoint<long> {
// public:
// };

// ==================== DATA GLOBALS / SINGLETONS ====================
// engine singletons the game references via copy relocations, plus C blitters.
extern void* LFB32_PutBitmap8;   // 0005e490
extern void* LFB32_VLineAdd;   // 0005e190
extern void* LFB16_Fill;   // 0005c6c0
extern void* SFB8_SaveBitmap;   // 0005fcd0
extern void* TimerSystem;   // 000aeff8
extern void* LFB8_PutBitmapC1_AMask;   // 0005ea10
extern void* LFB16_PutBitmap32;   // 0005d0c0
extern void* LDBRET_Broken;   // 000a0846
extern void* LFB24_Fill;   // 0005d450
extern void* LFB16_HLineSub;   // 0005c800
extern void* LFB16_PutBitmap;   // 0005c4e0
extern void* CC_LOWER;   // 000c5f40
extern void* _DYNAMIC;   // 000c536c
extern void* LDBRET_FileNotFound;   // 000a0848
extern void* LFB24_PutBitmap8Add;   // 0005d9c0
extern void* safefilelog;   // 000c6a00
extern void* _etext;   // 00098c96
extern void* StdConv;   // 000c6640
extern void* LFB15_FillAlfa;   // 0005c4d0
extern void* Palette16;   // 000c5a20
extern void* IPCSystem;   // 000aeea0
extern void* LFB16_FillAdd;   // 0005d100
extern void* LFB32_VLine;   // 0005e0b0
extern void* LFB15_VLineAdd;   // 0005bc80
extern void* EnvSystem;   // 000c54a8
extern void* LFB16_PutBitmapAdd;   // 0005c590
extern void* LFB24_PutBitmapSub;   // 0005d340
extern void* LFB16_VLineAlfa;   // 0005c940
extern void* WaitVBlank;   // 00091b70
extern void* LFB8_HLine;   // 0005ea20
extern void* LFB16_PutBitmap8_AMask;   // 0005cb70
extern void* LFB24_PutBitmapAdd;   // 0005d270
extern void* LFB24_VLineAdd;   // 0005d5a0
extern void* LFB16_PutBitmap8Add;   // 0005ca60
extern void* LFB24_PutBitmapAlfa;   // 0005d1b0
extern void* LFB16_PutBitmap8AlfaC1_AMask;   // 0005cc80
extern void* filelog;   // 000c6a08
extern void* Intuition_Mode;   // 000aeff0
extern void* LFB15_PutBitmap32;   // 0005c470
extern void* LFB16_PutBitmap8C1_LAMask;   // 0005cf80
extern void* SF_Void;   // 000a73c0
extern void* VBlankInProgress;   // 000aefe8
extern void* SFB8_PutBitmapC1_AMask;   // 0005fd30
extern void* LFB16_VLineAdd;   // 0005c790
extern void* CmpMem;   // 00091bb0
extern void* LFB32_PutBitmap24Add;   // 0005de60
extern void* LFB32_PutBitmap8Sub;   // 0005e690
extern void* SF_UnsignedWord_BE;   // 000a73c5
extern void* LFB24_PutBitmap8;   // 0005d8a0
extern void* SystemMemory;   // 000aef8c
extern void* LFB32_PutBitmap8_AMask;   // 0005e760
extern void* LFB32_HLineAlfa;   // 0005e350
extern void* _init;   // 000421e0
extern void* inputfile;   // 000c6900
extern void* LFB32_PutBitmap24Sub;   // 0005df10
extern void* Intuition;   // 000aefe4
extern void* LFB32_PutBitmap8Alfa;   // 0005e4e0
extern void* NULLSample8;   // 000aefbc
extern void* LFB24_FillAdd;   // 0005dd00
extern void* LFB15_HLineAlfa;   // 0005bdd0
extern void* LFB32_HLineSub;   // 0005e230
extern void* LFB24_HLineAlfa;   // 0005d760
extern void* SF_SignedByte;   // 000a73c2
extern void* LFB32_PutBitmap32;   // 0005dd30
extern void* SystemPointer;   // 000aef9c
extern void* CC_UPPER;   // 000c5e40
extern void* LFB24_VLineSub;   // 0005d6d0
extern void* LFB15_PutBitmap8C1_LAMask;   // 0005c330
extern void* LFB16_HLine;   // 0005c6e0
extern void* FreeHeapBlock;   // 000a0100
extern void* CT_FILENAME;   // 000c6040
extern void* LFB24_PutBitmap8Alfa;   // 0005d8f0
extern void* LFB16_PutBitmap8;   // 0005c9b0
extern void* MSG2;   // 000aefc8
extern void* LFB16_PutBitmap_AMask;   // 0005c690
extern void* CT_ALPHANUM;   // 000c6140
extern void* LFB24_VLine;   // 0005d4d0
extern void* SF_UnsignedByte;   // 000a73c1
extern void* LFB16_PutBitmap8C1_AMask;   // 0005cbb0
extern void* LFB16_PutBitmap8C1_Add;   // 0005cda0
extern void* LFB16_PutBitmap8Sub;   // 0005caf0
extern void* LFB24_HLineAdd;   // 0005d500
extern void* stdlog;   // 000c6a10
extern void* Objmem_Fill;   // 000aef98
extern void* LFB16_PutBitmapAlfa;   // 0005c510
extern void* LFB32_HLineAdd;   // 0005e0f0
extern void* LFB8_PutBitmap;   // 0005e920
extern void* LFB24_PutBitmap_AMask;   // 0005d400
extern void* NoTimerInterruptPaintFlag;   // 000aefe9
extern void* RunInBackground;   // 000c6c14
extern void* LFB15_VLineAlfa;   // 0005be30
extern void* SFB16_PutBitmap_AMask;   // 0005fc50
extern void* LFB8_VLine;   // 0005ea50
extern void* LFB32_PutBitmap24Alfa;   // 0005dde0
extern void* SFB8_PutBitmap_AMask;   // 0005fcf0
extern void* VCD;   // 000aefa4
extern void* LFB24_PutBitmap8C1_AMask;   // 0005dbd0
extern void* SFB8_PutBitmap;   // 0005fcb0
extern void* VModeInfo;   // 000c6740
extern void* LFB32_HLine;   // 0005e070
extern void* LFB16_VLineSub;   // 0005c870
extern void* __bss_start;   // 000c542c
extern void* SF_SignedWord_BE;   // 000a73c6
extern void* LFB8_PutBitmap_AMask;   // 0005e970
extern void* Palette15;   // 000c5c20
extern void* main;   // 000951e0
extern void* SFB16_PutBitmap;   // 0005fc10
extern void* BlankScreen;   // 000c68c0
extern void* RandomServer;   // 000c5e20
extern void* SF_SIZEOF;   // 000a73c7
extern void* NULLSample16;   // 000aefb4
extern void* LFB16_HLineAdd;   // 0005c720
extern void* SF_UnsignedWord_LE;   // 000a73c3
extern void* LFB15_PutBitmap8Alfa;   // 0005bea0
extern void* LFB15_HLineSub;   // 0005bcf0
extern void* SF_SignedWord_LE;   // 000a73c4
extern void* MessagePort;   // 000aefa0
extern void* LFB16_FillSub;   // 0005d120
extern void* Palette24;   // 000c5620
extern void* Frame_Counter;   // 000aefd0
extern void* LF_FileName;   // 000a0844
extern void* _fini;   // 00098ca0
extern void* LinuxTimer;   // 000c6a20
extern void* LFB24_PutBitmap8_AMask;   // 0005db70
extern void* LFB8_Fill;   // 0005e9c0
extern void* LFB24_HLineSub;   // 0005d640
extern void* VKeyboard;   // 000aef88
extern void* LFB32_FillAlfa;   // 0005e910
extern void* LFB32_VLineSub;   // 0005e2c0
extern void* AllocatedHeapBlock;   // 000a01e0
extern void* LFB32_PutBitmap8Add;   // 0005e5b0
extern void* LFB32_PutBitmap24_AMask;   // 0005dfb0
extern void* LFB32_VLineAlfa;   // 0005e3f0
extern void* LFB16_HLineAlfa;   // 0005c8e0
extern void* LFB15_PutBitmap8Add;   // 0005bf20
extern void* __builtin_vec_delete;   // 00096190
extern void* LFB15_PutBitmap8C1_Add;   // 0005c150
extern void* Sample_Size;   // 000aefa8
extern void* LFB32_PutBitmap24;   // 0005dd80
extern void* LDBRET_UnresolvedKey;   // 000a0847
extern void* _edata;   // 000c542c
extern void* CT_ALPHALOWER;   // 000c6440
extern void* LFB24_HLine;   // 0005d4a0
extern void* SFB16_Fill;   // 0005fc90
extern void* SFB16_SaveBitmap;   // 0005fc30
extern void* _end;   // 000c7440
extern void* CT_ALPHAUPPER;   // 000c6340
extern void* LFB16_PutBitmapSub;   // 0005c610
extern void* VMouse;   // 000aef90
extern void* MSG1;   // 000aefc4
extern void* LFB15_VLineSub;   // 0005bd60
extern void* LFB24_FillAlfa;   // 0005dd20
extern void* LocaleDataBase;   // 000aee9c
extern void* LFB15_PutBitmapSub;   // 0005bb90
extern void* LFB32_Fill;   // 0005e020
extern void* LFB15_FillSub;   // 0005c4c0
extern void* CaptureFlag;   // 000aefea
extern void* LFB24_PutBitmap;   // 0005d160
extern void* HBRET2Text_;   // 000aee8c
extern void* LFB24_VLineAlfa;   // 0005d800
extern void* CT_NUM;   // 000c6540
extern void* LFB16_PutBitmap8Alfa;   // 0005c9e0
extern void* LFB32_FillSub;   // 0005e900
extern void* LFB16_FillAlfa;   // 0005d140
extern void* LFB32_PutBitmap8C1_AMask;   // 0005e7c0
extern void* LFB15_PutBitmapAlfa;   // 0005ba90
extern void* CopyMem;   // 00091b80
extern void* CT_ALPHA;   // 000c6240
extern void* LFB15_FillAdd;   // 0005c4b0
extern void* LFB15_HLineAdd;   // 0005bc10
extern void* LFB15_PutBitmapAdd;   // 0005bb10
extern void* SFB8_Fill;   // 0005fd10
extern void* SFB16_PutBitmap8_AMask;   // 0005fc70
extern void* LDBRET_Ok;   // 000a0845
extern void* VVC;   // 000aefcc
extern void* MsgCenter;   // 000aefc0
extern void* LFB24_FillSub;   // 0005dd10
extern void* LFB15_PutBitmap8AlfaC1_AMask;   // 0005c030
extern void* LFB15_PutBitmap8Sub;   // 0005bfb0
extern void* LFB24_PutBitmap8Sub;   // 0005daa0
extern void* LFB32_FillAdd;   // 0005e8f0
extern void* LFB16_VLine;   // 0005c700
extern void* SoundCard;   // 000aefb0

// ==================== FREE FUNCTIONS (68) ====================
/*ret*/ VM_GetSoundCard();
/*ret*/ Print_Dec(long);
/*ret*/ GetWorkingDirectory();
/*ret*/ PutText(cGD &, cFont &, const char *, tPoint<long>, const cRectangle &, cPaletteFull *);
/*ret*/ VM_GetVVC();
/*ret*/ VM_GetLocaleDataBase();
/*ret*/ Clip(cRectangle &, tPoint<long> &, const cRectangle &);
/*ret*/ Fatal(char *);
/*ret*/ SVGALIB_Init();
/*ret*/ StrCopy(const char *, char *, unsigned long);
/*ret*/ SaveAsBMP_15(cBitmap &, const char *);
/*ret*/ SaveAsMVP(cBitmap &, const char *);
/*ret*/ GetLength(const char *);
/*ret*/ StrCmp(const char *, const char *);
/*ret*/ SVGAMOUSE_Init();
/*ret*/ GetPixelSize(eBMType);
/*ret*/ PutText(cGD &, cFont &, const char *, tPoint<long>, cPaletteFull *);
/*ret*/ SaveAsBMP_16(cBitmap &, const char *);
/*ret*/ VM_GetFrameCounter();
/*ret*/ keytable(eKeyCode, bool);
/*ret*/ HBRET2Text(unsigned char);
/*ret*/ SetWorkingDirectory(const char *);
/*ret*/ VM_GetVCD();
/*ret*/ CloseAllInputFiles();
/*ret*/ CalcSampleLength(cSoundFormat, bool, unsigned long);
/*ret*/ External_PlayAnim(const char *, const char *, const cRectangle &);
/*ret*/ PutText(cGD &, cFont &, const char *, tPoint<long>, const cRectangle &, eTextAlignment, cPaletteFull *);
/*ret*/ PushKeyInput();
/*ret*/ PlayFLCFile(char *, cGD &, const tPoint<long> &, int);
/*ret*/ CreateTCPIPServer(unsigned long, unsigned long, bool);
/*ret*/ Print_Text(const char *);
/*ret*/ CreateHeap_Compatibility(unsigned long);
/*ret*/ GetLogDriveString(unsigned short, char *);
/*ret*/ Print_Hex(unsigned long);
/*ret*/ Random(long, long);
/*ret*/ Copy(const char *, char *, unsigned long);
/*ret*/ SwapBuffers();
/*ret*/ PushMouseInput();
/*ret*/ MsgBox(char *, cFont &, cBitmap &, cFont &, cFont &);
/*ret*/ GetFileSystemID(const char *);
/*ret*/ VM_GetCDRomName();
/*ret*/ GetTextLength(cFont &, const char *);
/*ret*/ MouseRefresh();
/*ret*/ IdentifyFileSystem(const char *);
/*ret*/ operator+(const char *, cString &);
/*ret*/ VM_GetIPCSystem();
/*ret*/ CreateTCPIPClient(const char *, unsigned long, bool);
/*ret*/ SaveAsBMP(cBitmap &, const char *);
/*ret*/ ScreenShot(const char *);
/*ret*/ _TimerFunction(int);

#endif // MVOS_API_HPP

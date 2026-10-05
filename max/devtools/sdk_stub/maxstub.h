// Minimal stand-in for the 3ds Max SDK + Win32, used ONLY to syntax-check
// TopoWeave's Max layer on Linux/CI. It is NOT the SDK and cannot build a
// working plug-in. Signatures follow the official SDK reference.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cmath>
#include <string>
// ---------------- Win32 ----------------
typedef void* HWND; typedef void* HINSTANCE; typedef void* HACCEL; typedef void* HCURSOR; typedef void* LPVOID;
typedef int BOOL; typedef unsigned long ULONG; typedef unsigned long DWORD; typedef unsigned short USHORT; typedef unsigned int UINT;
typedef intptr_t INT_PTR; typedef intptr_t LONG_PTR; typedef uintptr_t WPARAM; typedef intptr_t LPARAM; typedef long LONG; typedef short SHORT;
typedef const wchar_t* LPCWSTR;
struct POINT { LONG x, y; }; struct RECT { LONG left, top, right, bottom; };
#define TRUE 1
#define FALSE 0
#define CALLBACK
#define WINAPI
#define DLL_PROCESS_ATTACH 1
#define MAKEINTRESOURCE(i) ((LPCWSTR)(uintptr_t)(i))
#define LOWORD(l) ((unsigned short)((uintptr_t)(l) & 0xffff))
#define HIWORD(l) ((unsigned short)(((uintptr_t)(l) >> 16) & 0xffff))
#define BN_CLICKED 0
#define BST_CHECKED 1
#define BST_UNCHECKED 0
#define WM_INITDIALOG 0x110
#define WM_COMMAND 0x111
#define WM_CLOSE 0x10
#define WM_MOVE 0x3
#define SW_HIDE 0
#define SW_SHOWNOACTIVATE 4
#define SWP_NOSIZE 1
#define SWP_NOZORDER 4
#define SWP_NOACTIVATE 16
#define GWLP_USERDATA (-21)
#define MB_OK 0
#define MB_YESNO 4
#define MB_ICONINFORMATION 0x40
#define MB_ICONQUESTION 0x20
#define IDYES 6
#define VK_RBUTTON 2
#define IDC_CROSS ((LPCWSTR)32515)
typedef INT_PTR (CALLBACK* DLGPROC)(HWND, UINT, WPARAM, LPARAM);
HWND CreateDialogParam(HINSTANCE, LPCWSTR, HWND, DLGPROC, LPARAM);
BOOL DestroyWindow(HWND); BOOL ShowWindow(HWND, int); BOOL IsWindowVisible(HWND);
BOOL CheckDlgButton(HWND, int, UINT); UINT IsDlgButtonChecked(HWND, int); BOOL EnableWindow(HWND, BOOL); HWND GetDlgItem(HWND, int);
BOOL SetDlgItemTextW(HWND, int, LPCWSTR); BOOL SetDlgItemText(HWND, int, LPCWSTR);
int MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT); BOOL GetWindowRect(HWND, RECT*);
BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT); LONG_PTR GetWindowLongPtr(HWND, int); LONG_PTR SetWindowLongPtr(HWND, int, LONG_PTR);
HCURSOR LoadCursor(HINSTANCE, LPCWSTR); HCURSOR SetCursor(HCURSOR); SHORT GetAsyncKeyState(int);
HACCEL LoadAccelerators(HINSTANCE, LPCWSTR); int LoadString(HINSTANCE, UINT, wchar_t*, int); BOOL DisableThreadLibraryCalls(HINSTANCE);
// ---------------- Max basics ----------------
typedef wchar_t MCHAR;
#define _M(x) L##x
#define MAX_RELEASE 27000
#define VERSION_3DSMAX 0x1234
class MSTR { public: MSTR() {} MSTR(const MCHAR* s) : s_(s ? s : L"") {} const MCHAR* data() const { return s_.c_str(); } MSTR& operator=(const MCHAR* s) { s_ = s; return *this; } private: std::wstring s_; };
typedef int TimeValue; typedef unsigned long SClass_ID; typedef unsigned long ChannelMask; typedef unsigned long PartID; typedef unsigned int RefMessage;
typedef short BlockID; typedef short ParamID; typedef int IOResult;
enum RefResult { REF_FAIL, REF_SUCCEED, REF_DONTCARE, REF_STOP };
#define IO_OK 0
#define PART_ALL 0xffffffff
#define REFMSG_CHANGE 0x21
#define GEOM_CHANNEL 1
#define TOPO_CHANNEL 2
#define SELECT_CHANNEL 4
#define TEXMAP_CHANNEL 8
#define VERTCOLOR_CHANNEL 16
#define GEOM_CHAN_NUM 1
#define TOPO_CHAN_NUM 0
#define SELECT_CHAN_NUM 3
#define TEXMAP_CHAN_NUM 2
#define VERT_COLOR_CHAN_NUM 7
#define OSM_CLASS_ID 0x810
#define GEOMOBJECT_CLASS_ID 0x10
class Class_ID { public: Class_ID() {} Class_ID(unsigned long, unsigned long) {} bool operator==(const Class_ID&) const { return true; } };
extern Class_ID polyObjectClassID, triObjectClassID;
class Interval { public: Interval() {} Interval(TimeValue, TimeValue) {} };
#define FOREVER Interval(-1, -1)
class Point3 { public: float x = 0, y = 0, z = 0; Point3() {} Point3(float a, float b, float c) : x(a), y(b), z(c) {}
  float& operator[](int i) { return i == 0 ? x : i == 1 ? y : z; } float operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
  Point3 operator+(const Point3& o) const { return Point3(x + o.x, y + o.y, z + o.z); } Point3 operator-(const Point3& o) const { return Point3(x - o.x, y - o.y, z - o.z); } };
Point3 Normalize(const Point3&);
class IPoint2 { public: int x = 0, y = 0; IPoint2() {} IPoint2(int a, int b) : x(a), y(b) {} };
class IPoint3 { public: int x = 0, y = 0, z = 0; IPoint3() {} IPoint3(int a, int b, int c) : x(a), y(b), z(c) {} };
class Matrix3 { public: Matrix3() {} explicit Matrix3(int) {} int operator==(const Matrix3&) const; Matrix3 operator*(const Matrix3&) const; };
Point3 operator*(const Point3&, const Matrix3&); Matrix3 Inverse(const Matrix3&); Point3 VectorTransform(const Matrix3&, const Point3&);
class Ray { public: Point3 p, dir; };
class Box3 { public: void Init(); Box3& operator+=(const Point3&); };
class Color { public: float r, g, b; Color(float a, float b_, float c) : r(a), g(b_), b(c) {} };
template <class T> class Tab { public: int Count() const; T& operator[](int); };
class INode;
class INodeTab : public Tab<INode*> { public: void DisposeTemporary(); };
class Animatable { public: virtual ~Animatable() {} virtual Class_ID ClassID() = 0; virtual SClass_ID SuperClassID() = 0; virtual void GetClassName(MSTR& s, bool localized = true) const = 0;
  virtual void DeleteThis() = 0; virtual int NumSubs() { return 0; } virtual Animatable* SubAnim(int) { return nullptr; } virtual MSTR SubAnimName(int, bool localized = true) { return MSTR(); }
  virtual int NumParamBlocks() { return 0; } virtual class IParamBlock2* GetParamBlock(int) { return nullptr; } virtual class IParamBlock2* GetParamBlockByID(BlockID) { return nullptr; } };
class ReferenceTarget; typedef ReferenceTarget* RefTargetHandle;
class RemapDir { public: RefTargetHandle CloneRef(RefTargetHandle); };
class ISave { public: void BeginChunk(USHORT); void EndChunk(); IOResult Write(const void*, ULONG, ULONG*); };
class ILoad { public: IOResult OpenChunk(); USHORT CurChunkID(); IOResult CloseChunk(); IOResult Read(void*, ULONG, ULONG*); };
class ReferenceMaker : public Animatable { public: virtual int NumRefs() { return 0; } virtual RefTargetHandle GetReference(int) { return nullptr; }
  virtual RefResult NotifyRefChanged(const Interval&, RefTargetHandle, PartID&, RefMessage, BOOL) = 0;
  virtual IOResult Save(ISave*) { return IO_OK; } virtual IOResult Load(ILoad*) { return IO_OK; } RefResult ReplaceReference(int, RefTargetHandle);
  protected: virtual void SetReference(int, RefTargetHandle) {} };
class ReferenceTarget : public ReferenceMaker { public: virtual RefTargetHandle Clone(RemapDir&) { return nullptr; }
  RefResult NotifyDependents(const Interval&, PartID, RefMessage); void BaseClone(ReferenceTarget*, ReferenceTarget*, RemapDir&); };
class Object; class GraphicsWindow; class ViewExp; class ModContext; class IObjParam; class CreateMouseCallBack;
struct ObjectState { Object* obj; };
class INode : public ReferenceTarget { public: Matrix3 GetObjectTM(TimeValue, Interval* = nullptr); const ObjectState& EvalWorldState(TimeValue, BOOL = TRUE); INode* GetActualINode(); };
class BaseObject : public ReferenceTarget { public: virtual CreateMouseCallBack* GetCreateMouseCallBack() = 0; virtual const MCHAR* GetObjectName(bool localized = true) const = 0;
  virtual void BeginEditParams(IObjParam*, ULONG, Animatable*) {} virtual void EndEditParams(IObjParam*, ULONG, Animatable*) {} };
class Object : public BaseObject { public: BOOL IsSubClassOf(Class_ID); virtual int CanConvertToType(Class_ID); virtual Object* ConvertToType(TimeValue, Class_ID); void MaybeAutoDelete(); void UpdateValidity(int, Interval); };
#define MN_DEAD 1
#define MNM_SL_OBJECT 0
class MNVert { public: Point3 p; int GetFlag(DWORD) const; };
class MNFace { public: int deg; int* vtx; DWORD smGroup; int GetFlag(DWORD) const; void MakePoly(int, int*, bool* = nullptr, bool* = nullptr); };
class MNMesh { public: int numv, numf; DWORD selLevel; MNVert* V(int); MNFace* F(int); void ClearAndFree(); void setNumVerts(int); void setNumFaces(int);
  void InvalidateGeomCache(); void InvalidateTopoCache(); void FillInMesh(); };
class PolyObject : public Object { public: MNMesh& GetMesh(); };
class Face { public: DWORD v[3]; };
class Mesh { public: int getNumVerts() const; Point3& getVert(int); int getNumFaces() const; Face* faces; };
class TriObject : public Object { public: Mesh& GetMesh(); };
class ModContextList : public Tab<ModContext*> {};
class Modifier : public BaseObject { public: virtual Interval LocalValidity(TimeValue) { return FOREVER; } virtual ChannelMask ChannelsUsed() = 0; virtual ChannelMask ChannelsChanged() = 0;
  virtual Class_ID InputType() = 0; virtual BOOL ChangeTopology() { return TRUE; } virtual void ModifyObject(TimeValue, ModContext&, ObjectState*, INode*) = 0;
  virtual int Display(TimeValue, INode*, ViewExp*, int, ModContext*) { return 0; } virtual void GetWorldBoundBox(TimeValue, INode*, ViewExp*, Box3&, ModContext*) {}
  IOResult Save(ISave*) override; IOResult Load(ILoad*) override; };
// ---------------- Graphics ----------------
enum ColorType { LINE_COLOR, TEXT_COLOR };
enum MarkerType { POINT_MRKR, DOT_MRKR };
#define GW_ILLUM 0x2
#define REDRAW_INTERACTIVE 1
#define REDRAW_NORMAL 2
class GraphicsWindow { public: DWORD getRndLimits() const; void setRndLimits(DWORD); void setTransform(const Matrix3&); void setColor(ColorType, float, float, float);
  void startSegments(); void segment(Point3*, int); void endSegments(); void startMarkers(); void marker(Point3*, MarkerType); void endMarkers();
  void polyline(int, Point3*, Point3*, int, int*); void wPolyline(int, IPoint3*, Point3*, int, int*); void wText(IPoint3*, const MCHAR*); int getWinSizeX(); int getWinSizeY(); };
class ViewExp { public: bool IsAlive() const; void MapScreenToWorldRay(float, float, Ray&); GraphicsWindow* getGW(); HWND GetHWnd(); };
// ---------------- Mouse / command modes ----------------
#define MOUSE_ABORT 0
#define MOUSE_IDLE 0
#define MOUSE_POINT 1
#define MOUSE_MOVE 2
#define MOUSE_DBLCLICK 3
#define MOUSE_INIT 4
#define MOUSE_UNINIT 5
#define MOUSE_FREEMOVE 6
#define MOUSE_KEYBOARD 7
#define MOUSE_PROPCLICK 8
#define MOUSE_SNAPCLICK 9
#define MOUSE_SHIFT (1 << 0)
#define MOUSE_CTRL (1 << 1)
#define MOUSE_ALT (1 << 2)
#define CLICK_DRAG_CLICK 1
class MouseCallBack { public: virtual ~MouseCallBack() {} virtual int proc(HWND, int, int, int, IPoint2) = 0; virtual int override(int mode) { return mode; } };
class ChangeForegroundCallback {}; extern ChangeForegroundCallback* CHANGE_FG_SELECTED_PTR;
#define CHANGE_FG_SELECTED CHANGE_FG_SELECTED_PTR
#define MODIFY_COMMAND 9
#define CID_USER 0x0000ffff
class CommandMode { public: virtual ~CommandMode() {} virtual int Class() = 0; virtual int ID() = 0; virtual MouseCallBack* MouseProc(int*) = 0;
  virtual ChangeForegroundCallback* ChangeFGProc() = 0; virtual BOOL ChangeFG(CommandMode*) = 0; virtual void EnterMode() = 0; virtual void ExitMode() = 0; };
// ---------------- Interface ----------------
class HitByNameDlgCallback { public: virtual ~HitByNameDlgCallback() {} virtual const MCHAR* dialogTitle() { return _M(""); } virtual const MCHAR* buttonText() { return _M(""); }
  virtual BOOL singleSelect() { return FALSE; } virtual BOOL useFilter() { return TRUE; } virtual int filter(INode*) { return TRUE; } virtual BOOL useProc() { return TRUE; } virtual void proc(INodeTab&) {} };
class ActionTable; class ActionCallback;
typedef DWORD ActionTableId; typedef DWORD ActionContextId;
class IActionManager { public: BOOL RegisterActionContext(ActionContextId, const MCHAR*); BOOL ActivateActionTable(ActionCallback*, ActionTableId); BOOL DeactivateActionTable(ActionCallback*, ActionTableId); };
class Interface { public: ViewExp& GetViewExp(HWND); TimeValue GetTime(); void RedrawViews(TimeValue, DWORD = REDRAW_NORMAL, ReferenceTarget* = nullptr);
  void GetModContexts(ModContextList&, INodeTab&); void PushPrompt(const MCHAR*); void PopPrompt(); void ReplacePrompt(const MCHAR*);
  CommandMode* GetCommandMode(); void PushCommandMode(CommandMode*); void PopCommandMode(); void DeleteMode(CommandMode*);
  HWND GetMAXHWnd(); void RegisterDlgWnd(HWND); int UnRegisterDlgWnd(HWND); BOOL DoHitByNameDialog(HitByNameDlgCallback* = nullptr); IActionManager* GetActionManager(); };
class IObjParam : public Interface {};
Interface* GetCOREInterface();
// ---------------- Undo ----------------
class RestoreObj { public: virtual ~RestoreObj() {} virtual void Restore(int isUndo) = 0; virtual void Redo() = 0; virtual int Size() { return 1; } virtual MSTR Description() { return MSTR(_M("---")); } };
class Hold { public: void Begin(); void Put(RestoreObj*); void Accept(const MCHAR*); void Cancel(); };
extern Hold theHold;
// ---------------- Param blocks ----------------
class IParamMap2;
class IParamBlock2 : public ReferenceTarget { public: BlockID ID(); int GetInt(ParamID, TimeValue = 0, int = 0); float GetFloat(ParamID, TimeValue = 0, int = 0); Color GetColor(ParamID, TimeValue = 0, int = 0);
  INode* GetINode(ParamID, TimeValue = 0, int = 0); BOOL SetValue(ParamID, TimeValue, int, int = 0); BOOL SetValue(ParamID, TimeValue, float, int = 0);
  int Count(ParamID); void ZeroCount(ParamID); int Append(ParamID, int, INode**, int = 0); ParamID LastNotifyParamID(); IParamMap2* GetMap(int = 0); ReferenceMaker* GetOwner(); };
class IParamMap2 { public: void Enable(ParamID, BOOL); IParamBlock2* GetParamBlock(); };
class ParamMap2UserDlgProc { public: virtual ~ParamMap2UserDlgProc() {} virtual INT_PTR DlgProc(TimeValue, IParamMap2*, HWND, UINT, WPARAM, LPARAM) = 0; virtual void DeleteThis() = 0; };
struct PB2Value { ReferenceTarget* r; };
class PBValidator { public: virtual ~PBValidator() {} virtual BOOL Validate(PB2Value&) = 0; virtual BOOL Validate(PB2Value& v, ReferenceMaker*, ParamID, int) { return Validate(v); } };
class ActionTable;
class ClassDesc { public: virtual ~ClassDesc() {} };
class ClassDesc2 : public ClassDesc { public: virtual int IsPublic() = 0; virtual void* Create(BOOL loading = FALSE) = 0; virtual const MCHAR* ClassName() = 0; virtual const MCHAR* NonLocalizedClassName() = 0;
  virtual SClass_ID SuperClassID() = 0; virtual Class_ID ClassID() = 0; virtual const MCHAR* Category() = 0; virtual const MCHAR* InternalName() { return nullptr; } virtual HINSTANCE HInstance() { return nullptr; }
  virtual int NumActionTables() { return 0; } virtual ActionTable* GetActionTable(int) { return nullptr; }
  void MakeAutoParamBlocks(ReferenceMaker*); void BeginEditParams(IObjParam*, ReferenceMaker*, ULONG, Animatable*); void EndEditParams(IObjParam*, ReferenceMaker*, ULONG, Animatable*); };
enum { TYPE_FLOAT, TYPE_INT, TYPE_RGBA, TYPE_BOOL, TYPE_WORLD, TYPE_INODE_TAB, TYPE_SPINNER, TYPE_SINGLECHEKBOX, TYPE_COLORSWATCH, TYPE_NODELISTBOX,
  EDITTYPE_INT, EDITTYPE_FLOAT, EDITTYPE_UNIVERSE, p_default, p_range, p_ui, p_validator, p_end, P_AUTO_CONSTRUCT = 0x100, P_AUTO_UI = 0x200, P_VARIABLE_SIZE = 0x400 };
#define SPIN_AUTOSCALE 0.1f
class ParamBlockDesc2 { public: ParamBlockDesc2(BlockID, const MCHAR*, int, ClassDesc2*, int, ...); };
// ---------------- Actions ----------------
struct ActionDescription { int mCmdID; int mDescriptionResourceID; int mShortNameResourceID; int mActionCategoryResourceID; };
class ActionTable { public: ActionTable(ActionTableId, ActionContextId, const MSTR&, HACCEL, int, const ActionDescription*, HINSTANCE); };
class ActionCallback { public: virtual ~ActionCallback() {} virtual BOOL ExecuteAction(int) = 0; };

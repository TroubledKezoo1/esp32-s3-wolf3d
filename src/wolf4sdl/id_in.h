//
//	ID Engine
//	ID_IN.h - Header file for Input Manager
//	v1.0d1
//	By Jason Blochowiak
//

#ifndef	__ID_IN__
#define	__ID_IN__

#include <unordered_map>

#ifdef	__DEBUG__
#define	__DEBUG_InputMgr__
#endif

typedef	int		ScanCode;
#define	sc_None			0
#define	sc_Bad			0xff
#define	sc_Return		13
#define	sc_Enter		sc_Return
#define	sc_Escape		27
#define	sc_Space		' '
#define	sc_BackSpace	8
#define	sc_Tab			9
#define	sc_Alt			128
#define	sc_Control		129
#define	sc_CapsLock		130
#define	sc_LShift		131
#define	sc_RShift		132
#define	sc_UpArrow		133
#define	sc_DownArrow	134
#define	sc_LeftArrow	135
#define	sc_RightArrow	136
#define	sc_Insert		137
#define	sc_Delete		127
#define	sc_Home			138
#define	sc_End			139
#define	sc_PgUp			140
#define	sc_PgDn			141
#define	sc_F1			142
#define	sc_F2			143
#define	sc_F3			144
#define	sc_F4			145
#define	sc_F5			146
#define	sc_F6			147
#define	sc_F7			148
#define	sc_F8			149
#define	sc_F9			150
#define	sc_F10			151
#define	sc_F11			152
#define	sc_F12			153

#define sc_ScrollLock		154
#define sc_PrintScreen		155

#define	sc_1			'1'
#define	sc_2			'2'
#define	sc_3			'3'
#define	sc_4			'4'
#define	sc_5			'5'
#define	sc_6			'6'
#define	sc_7			'7'
#define	sc_8			'8'
#define	sc_9			'9'
#define	sc_0			'0'

#define	sc_A			'a'
#define	sc_B			'b'
#define	sc_C			'c'
#define	sc_D			'd'
#define	sc_E			'e'
#define	sc_F			'f'
#define	sc_G			'g'
#define	sc_H			'h'
#define	sc_I			'i'
#define	sc_J			'j'
#define	sc_K			'k'
#define	sc_L			'l'
#define	sc_M			'm'
#define	sc_N			'n'
#define	sc_O			'o'
#define	sc_P			'p'
#define	sc_Q			'q'
#define	sc_R			'r'
#define	sc_S			's'
#define	sc_T			't'
#define	sc_U			'u'
#define	sc_V			'v'
#define	sc_W			'w'
#define	sc_X			'x'
#define	sc_Y			'y'
#define	sc_Z			'z'

#define	key_None		0

// [FG] map mouse wheel to key presses
const int KEYD_MWHEELUP = (0x80 + 0x6b), KEYD_MWHEELDOWN = (0x80 + 0x6c);

typedef	enum		{
						demo_Off,demo_Record,demo_Playback,demo_PlayDone
					} Demo;
typedef	enum		{
						ctrl_Keyboard,
						ctrl_Keyboard1 = ctrl_Keyboard,ctrl_Keyboard2,
						ctrl_Joystick,
						ctrl_Joystick1 = ctrl_Joystick,ctrl_Joystick2,
						ctrl_Mouse
					} ControlType;
typedef	enum		{
						motion_Left = -1,motion_Up = -1,
						motion_None = 0,
						motion_Right = 1,motion_Down = 1
					} Motion;
typedef	enum		{
						dir_North,dir_NorthEast,
						dir_East,dir_SouthEast,
						dir_South,dir_SouthWest,
						dir_West,dir_NorthWest,
						dir_None
					} Direction;
typedef	struct		{
						bool		btn0,btn1,btn2,btn3;
						short		x,y;
						Motion		xaxis,yaxis;
						Direction	dir;
					} WolfCursorInfo;
typedef	WolfCursorInfo	ControlInfo;
typedef	struct		{
						ScanCode	btn0,btn1,
									upleft,		up,		upright,
									left,				right,
									downleft,	down,	downright;
					} KeyboardDef;
typedef	struct		{
						uint16_t		joyMinX,joyMinY,
									threshMinX,threshMinY,
									threshMaxX,threshMaxY,
									joyMaxX,joyMaxY,
									joyMultXL,joyMultYL,
									joyMultXH,joyMultYH;
					} JoystickDef;
// Global variables
extern std::unordered_map<ScanCode, boolean> Keyboard;
extern           boolean    MousePresent;
extern  volatile boolean    Paused;
extern  volatile char       LastASCII;
extern           ScanCode   LastScan;
extern           int        JoyNumButtons;
extern           boolean    grabmouse;


// Function prototypes
#define	IN_KeyDown(code)	(Keyboard[(code)])
#define	IN_ClearKey(code)	{Keyboard[code] = false;\
							if (code == LastScan) LastScan = sc_None;}

// DEBUG - put names in prototypes
extern	void		IN_Startup(void),IN_Shutdown(void);
extern	void		IN_ClearKeysDown(void);
extern	void		IN_ReadControl(int,ControlInfo *);
extern	void		IN_GetJoyAbs(uint16_t joy,uint16_t *xp,uint16_t *yp);
extern	void		IN_SetupJoy(uint16_t joy,uint16_t minx,uint16_t maxx,
								uint16_t miny,uint16_t maxy);
extern	void		IN_StopDemo(void),IN_FreeDemoBuffer(void),
					IN_Ack(void);
extern bool  Wolf_IN_UserInput(uint32_t delay);
extern	char		IN_WaitForASCII(void);
extern	ScanCode	IN_WaitForKey(void);
extern	uint16_t		IN_GetJoyButtonsDB(uint16_t joy);
extern	const char *IN_GetScanName(ScanCode);

void    IN_WaitAndProcessEvents();
void    IN_ProcessEvents();

int     IN_MouseButtons (void);

boolean IN_JoyPresent();
void    IN_SetJoyCurrent(int joyIndex);
int     IN_JoyButtons (void);
void    IN_GetJoyDelta(int *dx,int *dy);
void    IN_GetJoyFineDelta(int *dx, int *dy);

void    IN_StartAck(void);
boolean IN_CheckAck (void);
void    IN_UpdateGrab(void);

#endif

















#define	e1PAGENOTFOUND	-110	

#define	e1WRONGBUFFER	-310	
#define	e1NULLFIDPARM	-510	
#define	e1NULLPIDPARM	-610	
#define	e1BADMODEPARM	-810	
#define	eNOMOREBUCKETS	-910	
#define	eNOENTRY	-911	
#define	e1PAGEWASFOUND	-912	
#define	e1PAGEWASFIXED	-913	

#define e1PTRNULL		-1010
#define e1PTROUTOFRANGE -1110
#define	e1NOFREEBUFFERS	-210	

#define	eNOERROR	0		

#define	CHECKERR(p,c) if((int)(c)<0) fatalerror(p,(int)(c))
#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "lcd.h"
#include "key.h"
#include "beep.h"
#include "sha256.h"
#include "touch.h"	  
#include "malloc.h" 
#include "usmart.h"  
#include "sdio_sdcard.h"    
#include "w25qxx.h"    
#include "ff.h"  
#include "exfuns.h"    
#include "fontupd.h"
#include "text.h"	
#include "usart2.h"
#include "AS608.h" 

#define usart2_baund  57600//串口2波特率，根据指纹模块波特率更改

SysPara AS608Para;//指纹模块AS608参数
u16 ValidN;//模块内有效指纹个数
u8** kbd_tbl;
const  u8* kbd_menu[15]={"删指纹"," : ","录指纹","1","2","3","4","5","6","7","8","9","DEL","0","Enter",};//按键表
const  u8* kbd_menu1[12]={"1","2","3","4","5","6","7","8","9","DEL","0","Enter"};//按键表
const  u8* kbd_delFR[15]={"返回"," : ","清空指纹","1","2","3","4","5","6","7","8","9","DEL","0","Enter",};//按键表
 

void Add_FR(void);	//录指纹
void Del_FR(void);	//删除指纹
void press_FR(void);//刷指纹
void ShowErrMessage(u8 ensure);//显示确认码错误信息
void AS608_load_keyboard(u16 x,u16 y,u8 **kbtbl,int select);//加载虚拟键盘
u8  AS608_get_keynum(u16 x,u16 y);//获取键盘数
u8  AS608_get_keynum1(u16 x,u16 y);//获取键盘数
u16 GET_NUM(void);//获取数值
void MIMA_SUO_mode();
int main(void)
{
	u8 ensure;
	u8 key_num;
	char *str;	

	Stm32_Clock_Init(336,8,2,7);//设置时钟,168Mhz 
	delay_init(168);			      //延时初始化  
	uart_init(84,115200); 			//初始化串口1，用于支持USMART
	BEEP_Init();								//初始化蜂鸣器
 	LCD_Init();									//LCD初始化  
 	KEY_Init();									//按键初始化  
	Sha256_test();        //计算预置密码哈希值
	W25QXX_Init();							//初始化W25Q128
	tp_dev.init();							//初始化触摸屏
	usart2_init(42,usart2_baund);//初始化串口2,用于与指纹模块通讯
	PS_StaGPIO_Init();					//初始化FR读状态引脚
	usmart_dev.init(168);				//初始化USMART
	my_mem_init(SRAMIN);				//初始化内部内存池 
	exfuns_init();							//为fatfs相关变量申请内存  
 	f_mount(fs[1],"1:",1); 			//挂载FLASH.
	POINT_COLOR=RED;
	while(font_init()) 					//检查字库
	{	    
		LCD_ShowString(60,50,240,16,16,"Font Error!");
		delay_ms(200);				  
		LCD_Fill(60,50,240,66,WHITE);//清除显示	
		delay_ms(200);		
	}
	if(!(tp_dev.touchtype&0x80))//如果是电阻屏
	{
		Show_Str_Mid(0,30,"是否进行触摸屏校准",16,240);
		POINT_COLOR=BLUE;
		Show_Str_Mid(0,60,"是:KEY2   否:KEY0",16,240);	
		while(1)
		{
			key_num=KEY_Scan(0);
			if(key_num==KEY0_PRES)
				break;
			if(key_num==KEY2_PRES)
			{
				LCD_Clear(WHITE);
				TP_Adjust();  	 //屏幕校准 
				TP_Save_Adjdata();//保存校准参数
				break;				
			}
		}
	}
 
	/*加载指纹识别实验界面*/			    	 		    	 
 	POINT_COLOR=BLUE;
	Show_Str_Mid(0,40,"与AS608模块握手....",16,240);	
	while(PS_HandShake(&AS608Addr))//与AS608模块握手
	{
		delay_ms(400);
		LCD_Fill(0,40,240,80,WHITE);
		Show_Str_Mid(0,40,"未检测到模块!!!",16,240);
		delay_ms(800);
		LCD_Fill(0,40,240,80,WHITE);
		Show_Str_Mid(0,40,"尝试连接模块...",16,240);		  
	}
	LCD_Fill(30,40,240,100,WHITE);
	Show_Str_Mid(0,40,"通讯成功!!!",16,240);
	str=mymalloc(SRAMIN,30);
	sprintf(str,"波特率:%d   地址:%x",usart2_baund,AS608Addr);
	Show_Str(0,60,240,16,(u8*)str,16,0);
	ensure=PS_ValidTempleteNum(&ValidN);//读库指纹个数
	if(ensure!=0x00)
		ShowErrMessage(ensure);//显示确认码错误信息	
	ensure=PS_ReadSysPara(&AS608Para);  //读参数 
	if(ensure==0x00)
	{
		mymemset(str,0,50);
		sprintf(str,"库容量:%d     对比等级: %d",AS608Para.PS_max-ValidN,AS608Para.PS_level);
		Show_Str(0,80,240,16,(u8*)str,16,0);
	}
	else
		ShowErrMessage(ensure);	
	myfree(SRAMIN,str);
	AS608_load_keyboard(0,170,(u8**)kbd_menu,1);//加载虚拟键盘
	while(1)
	{
		key_num=AS608_get_keynum(0,170);	
		if(key_num)
		{
			if(key_num==1)Del_FR();		//删指纹
			if(key_num==3)Add_FR();		//录指纹									
		}
		if(AS608Para.PS_max-ValidN>13)
		MIMA_SUO_mode();
//		if(PS_Sta)	 //检测PS_Sta状态，如果有手指按下
//		{
//			press_FR();//刷指纹			
//		}				 
	} 	
	
}

void MIMA_SUO_mode(){
		u8  pass_word=1,count = 3;
	u8 key_num,input_key_count = 0;
		u8 pass_word_num[9];
	 	LCD_Init();									//LCD初始化  
		/*加载密码锁密码输入界面*/
	LCD_Clear(WHITE);
	//POINT_COLOR=RED;
	Show_Str_Mid(0,0,"双认证密码锁测试程序",16,240);	
	AS608_load_keyboard(0,200,(u8**)kbd_menu1,0);//加载虚拟键盘
  Show_Str_Mid(0,40,"请输入密码:",16,240);

	while(pass_word){
			in_here:
			key_num=AS608_get_keynum1(0,200);	
		  if(input_key_count==9)goto TEN;
				if((key_num)&&!(key_num==12)&&!(key_num==10)){
					Show_Str_Mid(-60 + (input_key_count * 16),60,(u8*)kbd_menu1[key_num - 1],16,240);  //捕获键盘值
					pass_word_num[input_key_count] = key_num;
					input_key_count++;
				}
				TEN:
				if(key_num==10)
					{

						Show_Str_Mid(-60 + (input_key_count * 16),60,"                                         ",16,240); 
					  pass_word_num[input_key_count] = 0;
						input_key_count--;                                       //输入键值为10时执行DEL操作	
					}

				if((key_num==12))//&&(input_key_count==9))
		{
		BYTE buf[SHA256_BLOCK_SIZE];
		SHA256_CTX ctx;
		sha256_init(&ctx);
		sha256_update(&ctx, pass_word_num, 9);
		sha256_final(&ctx,buf,0);
			if(ertification_pass() == 0){
				 	LCD_Init();					//LCD初始化  
				Show_Str(0,80,200,16,"密码正确",16,0);
				Show_Str(30,65,200,16,"                                                                                  ",16,0);	//清除显示	 
        count=3;	
				Show_Str(0,120,200,16,"请刷指纹",16,0);			
			while(1){				
			if(PS_Sta)	 //检测PS_Sta状态，如果有手指按下
		{
			
			press_FR();//刷指纹		

		}				 	//停止该循环，启动指纹模块
			}	}
			else
			{
				Show_Str(150,45,200,16,"密码错误",16,0);
				for(input_key_count = 0;input_key_count < 9; input_key_count++)
				{
					pass_word_num[input_key_count] = 0;
					Show_Str((30+16*(input_key_count-1)),105,200,16," ",16,0);
				}
				input_key_count=0;

					Show_Str(30,65,200,16,(u8*)kbd_tbl[count-2],16,0);	
								count--;
				 	Show_Str(40,65,200,16,"次后锁定",16,0);	 
				if(count==0){ 	while(1)
							{LCD_Init();
				Show_Str(30,85,200,16,"!!!!!!!!!!!!!!!!!!!!!",16,0);	 
							}}
				goto in_here; 
			}
		}
		}
}

//加载按键界面（尺寸x,y为240*150）
//x,y:界面起始坐标（240*320分辨率的时候，x必须为0）
void AS608_load_keyboard(u16 x,u16 y,u8 **kbtbl,int select)
{
	u16 i;
	POINT_COLOR=RED;
	kbd_tbl=kbtbl;
	LCD_Fill(x,y,x+240,y+150,WHITE);
	LCD_DrawRectangle(x,y,x+240,y+150);						   
	LCD_DrawRectangle(x+80,y,x+160,y+150);	 
	LCD_DrawRectangle(x,y+30,x+240,y+60);
	LCD_DrawRectangle(x,y+90,x+240,y+120);
	POINT_COLOR=BLUE;
	if(select == 1)
	{
		for(i=0;i<15;i++)
		{
			if(i==1)//按键表第2个‘:’不需要中间显示
				Show_Str(x+(i%3)*80+2,y+7+30*(i/3),80,30,(u8*)kbd_tbl[i],16,0);	
			else
				Show_Str_Mid(x+(i%3)*80,y+7+30*(i/3),(u8*)kbd_tbl[i],16,80);
		} 
	}
	else
	{
				for(i=0;i<12;i++)
		{
//			if(i==1)//按键表第2个‘:’不需要中间显示
//				Show_Str(x+(i%3)*80+2,y+7+30*(i/3),80,30,(u8*)kbd_tbl[i],16,0);	
//			else
				Show_Str_Mid(x+(i%3)*80,y+7+30*(i/3),(u8*)kbd_tbl[i],16,80);
		}
	}
}
//按键状态设置
//x,y:键盘坐标
//key:键值(0~14)
//sta:状态，0，松开；1，按下；
//显示由白转绿再转白的效果
void AS608_key_staset(u16 x,u16 y,u8 keyx,u8 sta,int select)  
{		  
	u16 i=keyx/3,j=keyx%3;
	if(select == 1){
			if(keyx>16)return;
			if(sta &&keyx!=1)//按键表第2个‘:’不需要清除
				LCD_Fill(x+j*80+1,y+i*30+1,x+j*80+78,y+i*30+28,GREEN);
			else if(keyx!=1)
				LCD_Fill(x+j*80+1,y+i*30+1,x+j*80+78,y+i*30+28,WHITE);
			if(keyx!=1)//不是‘：’
				Show_Str_Mid(x+j*80,y+7+30*i,(u8*)kbd_tbl[keyx],16,80);
		}
	else{
			if(keyx>13)return;
			if(sta)
				LCD_Fill(x+j*80+1,y+i*30+1,x+j*80+78,y+i*30+28,GREEN);
			else
				LCD_Fill(x+j*80+1,y+i*30+1,x+j*80+78,y+i*30+28,WHITE);
				Show_Str_Mid(x+j*80,y+7+30*i,(u8*)kbd_tbl[keyx],16,80);
		}
}
//得到触摸屏的输入
//x,y:键盘坐标
//返回值:（1~15,对应按键表）
u8 AS608_get_keynum(u16 x,u16 y)
{
	u16 i,j;
	static u8 key_x=0;//0,没有任何按键按下
	u8 key=0;
	tp_dev.scan(0); 		 
	if(tp_dev.sta&TP_PRES_DOWN)//触摸屏被按下
	{	
		for(i=0;i<5;i++)
		{
			for(j=0;j<3;j++)
			{
			 	if(tp_dev.x[0]<(x+j*80+80)&&tp_dev.x[0]>(x+j*80)&&tp_dev.y[0]<(y+i*30+30)&&tp_dev.y[0]>(y+i*30))
				{	
					key=i*3+j+1;	 
					break;	 		   
				}
			}
			if(key)
			{	   
				if(key_x==key)key=0;
				else 
				{
					AS608_key_staset(x,y,key_x-1,0,1);
					key_x=key;
					AS608_key_staset(x,y,key_x-1,1,1);
				}
				break;
			}
	  }  
	}else if(key_x) 
	{
		AS608_key_staset(x,y,key_x-1,0,1);
		key_x=0;
	} 
	return key; 
}

//得到触摸屏的输入
//x,y:键盘坐标
//返回值:（1~15,对应按键表）
u8 AS608_get_keynum1(u16 x,u16 y)
{
	u16 i,j;
	static u8 key_x=0;//0,没有任何按键按下
	u8 key=0;
	tp_dev.scan(0); 		 
	if(tp_dev.sta&TP_PRES_DOWN)//触摸屏被按下
	{	
		for(i=0;i<4;i++)
		{
			for(j=0;j<3;j++)
			{
			 	if(tp_dev.x[0]<(x+j*80+80)&&tp_dev.x[0]>(x+j*80)&&tp_dev.y[0]<(y+i*30+30)&&tp_dev.y[0]>(y+i*30))
				{	
					key=i*3+j+1;	 
					break;	 		   
				}
			}
			if(key)
			{	   
				if(key_x==key)key=0;
				else 
				{
					AS608_key_staset(x,y,key_x-1,0,0);
					key_x=key;
					AS608_key_staset(x,y,key_x-1,1,0);
				}
				break;
			}
	  }  
	}else if(key_x) 
	{
		AS608_key_staset(x,y,key_x-1,0,0);
		key_x=0;
	} 
	return key; 
}

//获取键盘数值
u16 GET_NUM(void)
{
	u8  key_num=0;
	u16 num=0;
	while(1)
	{
		key_num=AS608_get_keynum(0,170);	
		if(key_num)
		{
			if(key_num==1)return 0xFFFF;//‘返回’键
			if(key_num==3)return 0xFF00;//		
			if(key_num>3&&key_num<13&&num<99)//‘1-9’键(限制输入3位数)
				num =num*10+key_num-3;		
			if(key_num==13)num =num/10;//‘Del’键			
			if(key_num==14&&num<99)num =num*10;//‘0’键
			if(key_num==15)return num;  //‘Enter’键
		}
		LCD_ShowNum(80+15,170+7,num,6,16);
	}	
}
//显示确认码错误信息
void ShowErrMessage(u8 ensure)
{
	LCD_Fill(0,120,lcddev.width,160,WHITE);
	Show_Str_Mid(0,120,(u8*)EnsureMessage(ensure),16,240);
}
//录指纹
void Add_FR(void)
{
	u8 i,ensure ,processnum=0;
	u16 ID;
	while(1)
	{
		switch (processnum)
		{
			case 0:
				i++;
				LCD_Fill(0,100,lcddev.width,160,WHITE);
				Show_Str_Mid(0,100,"请按指纹",16,240);
				ensure=PS_GetImage();
				if(ensure==0x00) 
				{
					BEEP=1;
					ensure=PS_GenChar(CharBuffer1);//生成特征
					BEEP=0;
					if(ensure==0x00)
					{
						LCD_Fill(0,120,lcddev.width,160,WHITE);
						Show_Str_Mid(0,120,"指纹正常",16,240);
						i=0;
						processnum=1;//跳到第二步						
					}else ShowErrMessage(ensure);				
				}else ShowErrMessage(ensure);						
				break;
			
			case 1:
				i++;
				LCD_Fill(0,100,lcddev.width,160,WHITE);
				Show_Str_Mid(0,100,"请按再按一次指纹",16,240);
				ensure=PS_GetImage();
				if(ensure==0x00) 
				{
					BEEP=1;
					ensure=PS_GenChar(CharBuffer2);//生成特征
					BEEP=0;
					if(ensure==0x00)
					{
						LCD_Fill(0,120,lcddev.width,160,WHITE);
						Show_Str_Mid(0,120,"指纹正常",16,240);
						i=0;
						processnum=2;//跳到第三步
					}else ShowErrMessage(ensure);	
				}else ShowErrMessage(ensure);		
				break;

			case 2:
				LCD_Fill(0,100,lcddev.width,160,WHITE);
				Show_Str_Mid(0,100,"对比两次指纹",16,240);
				ensure=PS_Match();
				if(ensure==0x00) 
				{
					LCD_Fill(0,120,lcddev.width,160,WHITE);
					Show_Str_Mid(0,120,"对比成功,两次指纹一样",16,240);
					processnum=3;//跳到第四步
				}
				else 
				{
					LCD_Fill(0,100,lcddev.width,160,WHITE);
					Show_Str_Mid(0,100,"对比失败，请重新录入指纹",16,240);
					ShowErrMessage(ensure);
					i=0;
					processnum=0;//跳回第一步		
				}
				delay_ms(1200);
				break;

			case 3:
				LCD_Fill(0,100,lcddev.width,160,WHITE);
				Show_Str_Mid(0,100,"生成指纹模板",16,240);
				ensure=PS_RegModel();
				if(ensure==0x00) 
				{
					LCD_Fill(0,120,lcddev.width,160,WHITE);
					Show_Str_Mid(0,120,"生成指纹模板成功",16,240);
					processnum=4;//跳到第五步
				}else {processnum=0;ShowErrMessage(ensure);}
				delay_ms(1200);
				break;
				
			case 4:	
				LCD_Fill(0,100,lcddev.width,160,WHITE);
				Show_Str_Mid(0,100,"请输入储存ID,按Enter保存",16,240);
				Show_Str_Mid(0,120,"0=< ID <=299",16,240);
				do
					ID=GET_NUM();
				while(!(ID<AS608Para.PS_max));//输入ID必须小于最大存储数值
				ensure=PS_StoreChar(CharBuffer2,ID);//储存模板
				if(ensure==0x00) 
				{			
					LCD_Fill(0,100,lcddev.width,160,WHITE);					
					Show_Str_Mid(0,120,"录入指纹成功",16,240);
					PS_ValidTempleteNum(&ValidN);//读库指纹个数
					LCD_ShowNum(56,80,AS608Para.PS_max-ValidN,3,16);
					delay_ms(1500);
					LCD_Fill(0,100,240,160,WHITE);
					return ;
				}else {processnum=0;ShowErrMessage(ensure);}					
				break;				
		}
		delay_ms(400);
		if(i==5)//超过5次没有按手指则退出
		{
			LCD_Fill(0,100,lcddev.width,160,WHITE);
			break;	
		}				
	}
}

//刷指纹
void press_FR(void)
{
	SearchResult seach;
	u8 ensure;
	char *str;
	ensure=PS_GetImage();
	if(ensure==0x00)//获取图像成功 
	{	
		BEEP=1;//打开蜂鸣器	
		ensure=PS_GenChar(CharBuffer1);
		if(ensure==0x00) //生成特征成功
		{		
			BEEP=0;//关闭蜂鸣器	
			ensure=PS_HighSpeedSearch(CharBuffer1,0,AS608Para.PS_max,&seach);
			if(ensure==0x00)//搜索成功
			{				
			  LCD_Init();									//LCD初始化  
				LCD_Fill(0,100,lcddev.width,160,WHITE);
				Show_Str_Mid(0,100,"刷指纹成功",16,240);				
				str=mymalloc(SRAMIN,50);
//				sprintf(str,"确有此人,ID:%d  匹配得分:%d",seach.pageID,seach.mathscore);
				Show_Str_Mid(0,140,"开锁成功",16,240);
				myfree(SRAMIN,str);
				while(1){}
			}
			else 
				ShowErrMessage(ensure);					
	  }
		else
			ShowErrMessage(ensure);
	 BEEP=0;//关闭蜂鸣器
	 delay_ms(600);
	 LCD_Fill(0,100,lcddev.width,160,WHITE);
	}
		
}

//删除指纹
void Del_FR(void)
{
	u8  ensure;
	u16 num;
	LCD_Fill(0,100,lcddev.width,160,WHITE);
	Show_Str_Mid(0,100,"删除指纹",16,240);
	Show_Str_Mid(0,120,"请输入指纹ID按Enter发送",16,240);
	Show_Str_Mid(0,140,"0=< ID <=299",16,240);
	delay_ms(50);
	AS608_load_keyboard(0,170,(u8**)kbd_delFR,1);
	num=GET_NUM();//获取返回的数值
	if(num==0xFFFF)
		goto MENU ; //返回主页面
	else if(num==0xFF00)
		ensure=PS_Empty();//清空指纹库
	else 
		ensure=PS_DeletChar(num,1);//删除单个指纹
	if(ensure==0)
	{
		LCD_Fill(0,120,lcddev.width,160,WHITE);
		Show_Str_Mid(0,140,"删除指纹成功",16,240);		
	}
  else
		ShowErrMessage(ensure);	
	delay_ms(1200);
	PS_ValidTempleteNum(&ValidN);//读库指纹个数
	LCD_ShowNum(56,80,AS608Para.PS_max-ValidN,3,16);
MENU:	
	LCD_Fill(0,100,lcddev.width,160,WHITE);
	delay_ms(50);
	AS608_load_keyboard(0,170,(u8**)kbd_menu,1);
}


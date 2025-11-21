#ifndef __INC_SERVICE_H__
#define __INC_SERVICE_H__

#define ENABLE_AUTODETECT_INTERNAL_IP
#define ENABLE_PROXY_IP
#define _IMPROVED_PACKET_ENCRYPTION_ // 패킷 암호화 개선
#define __PET_SYSTEM__
#define __UDP_BLOCK__

#define FIX_HEADER_CG_MARK_LOGIN	// Fix for syserr error header 100 (login phase does not handle this packet! header 100);
#define FIX_NEG_CMD_CORE_DOWNER		// Fix core downer after negative command value (mobs, items, etc...)
#define FIX_NEG_HP					// Fix negative HP value when dead
#define FIX_MESSENGER_ACTION_SYNC	// Fix companion messenger updates when being deleted by a friend

#endif

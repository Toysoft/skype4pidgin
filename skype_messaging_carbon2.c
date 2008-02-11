

#include <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>


typedef struct SkypeDelegate
{
	// Required member
	CFStringRef clientApplicationName;
	
	// Optional members, can be NULL
	void (*SkypeNotificationReceived)(CFStringRef aNotificationString);
	void (*SkypeAttachResponse)(unsigned int aAttachResponseCode);			// 0 - failed, 1 - success
	void (*SkypeBecameAvailable)(CFPropertyListRef aNotification);
	void (*SkypeBecameUnavailable)(CFPropertyListRef aNotification);
} SkypeDelegate;

static SkypeDelegate *delegate = NULL;
static int isavailable = 0;
static int client_id = 0;

char *
CFStringToCString(CFStringRef input)
{
	if (input == NULL)
		return NULL;
	int strlen = CFStringGetMaximumSizeForEncoding(CFStringGetLength(input), kCFStringEncodingUTF8);
	char *output = NewPtr(strlen+1);
	CFStringGetCString(input, output, strlen+1, kCFStringEncodingUTF8);
	return output;
}

int
CFNumberToCInt(CFNumberRef input)
{
	if (input == NULL)
		return 0;
	int output;
	CFNumberGetValue(input, kCFNumberIntType, &output);
	return output;
}

void
availabilityUpdateCallback(
	CFNotificationCenterRef center,
	void *observer,
	CFStringRef name,
	const void *object,
	CFDictionaryRef userInfo)
{
	CFNumberRef number = (CFNumberRef)CFDictionaryGetValue(userInfo, CFSTR("SKYPE_API_AVAILABILITY")); 
	isavailable = CFNumberToCInt(number);
}

void
debugCallback(
	CFNotificationCenterRef center,
	void *observer,
	CFStringRef name,
	const void *object,
	CFDictionaryRef userInfo)
{
	int i = 0;
	
	printf("Debug callback: %s\n", CFStringToCString(name));
	if (!userInfo)
		return;
	
	CFIndex count = CFDictionaryGetCount(userInfo);
	const void *keys[count];
	const void *values[count];
	CFDictionaryGetKeysAndValues(userInfo, keys, values);
	for(i = 0; i < count; i++)
	{
		printf("For i=%d, key: %s\n", i,
					CFStringToCString((CFStringRef)keys[i]));
	}
}


void
apiNotificationCallback(
	CFNotificationCenterRef center,
	void *observer,
	CFStringRef name,
	const void *object,
	CFDictionaryRef userInfo)
{
	CFNumberRef number = (CFNumberRef) CFDictionaryGetValue(userInfo, CFSTR("SKYPE_API_CLIENT_ID"));
	int client_number = CFNumberToCInt(number);
	if (client_number != 999 && (!client_id || client_id != client_number))
	{
		return;
	}
	CFStringRef string = (CFStringRef) CFDictionaryGetValue(userInfo, CFSTR("SKYPE_API_NOTIFICATION_STRING"));
	if (string && delegate && delegate->SkypeNotificationReceived)
	{
		delegate->SkypeNotificationReceived(string);
	}
}


void
attachResponseCallback(
	CFNotificationCenterRef center,
	void *observer,
	CFStringRef name,
	const void *object,
	CFDictionaryRef userInfo)
{
	CFNumberRef responseNumber = (CFNumberRef)CFDictionaryGetValue(userInfo, CFSTR("SKYPE_API_ATTACH_RESPONSE"));
	int response = CFNumberToCInt(responseNumber);
	client_id = response;
	if (delegate && delegate->SkypeAttachResponse)
	{
		delegate->SkypeAttachResponse(response?1:0);
	}
}


void
skypeQuitCallback(
	CFNotificationCenterRef center,
	void *observer,
	CFStringRef name,
	const void *object,
	CFDictionaryRef userInfo)
{
	if (delegate && delegate->SkypeBecameAvailable)
		delegate->SkypeBecameUnavailable(NULL);
}


void
skypeAvailableCallback(
	CFNotificationCenterRef center,
	void *observer,
	CFStringRef name,
	const void *object,
	CFDictionaryRef userInfo)
{
	if (delegate && delegate->SkypeBecameAvailable)
		delegate->SkypeBecameAvailable(NULL);
}


// STANDARD SKYPE.H BITS:
void RemoveSkypeDelegate(void);

void
SetSkypeDelegate(SkypeDelegate *aDelegate)
{
	if (!aDelegate->clientApplicationName)
	{
		printf("Deletegate requires application name\n");
		delegate = NULL;
		return;
	}
	
	if (delegate)
	{
		RemoveSkypeDelegate();
	}
	
	delegate = aDelegate;
	
	CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();
	
	CFNotificationCenterAddObserver(
		center,
		delegate->clientApplicationName,
		apiNotificationCallback,
		CFSTR("SKSkypeAPINotification"),
		NULL,
		CFNotificationSuspensionBehaviorDeliverImmediately);

	CFNotificationCenterAddObserver(
		center,
		delegate->clientApplicationName,
		skypeQuitCallback,
		CFSTR("SKSkypeWillQuit"),
		NULL,
		CFNotificationSuspensionBehaviorDeliverImmediately);

	CFNotificationCenterAddObserver(
		center,
		delegate->clientApplicationName,
		skypeAvailableCallback,
		CFSTR("SKSkypeBecameAvailable"),
		NULL,
		CFNotificationSuspensionBehaviorDeliverImmediately);

	CFNotificationCenterAddObserver(
		center,
		delegate->clientApplicationName,
		availabilityUpdateCallback,
		CFSTR("SKAvailabilityUpdate"),
		NULL,
		CFNotificationSuspensionBehaviorDeliverImmediately);

	CFNotificationCenterAddObserver(
		center,
		delegate->clientApplicationName,
		attachResponseCallback,
		CFSTR("SKSkypeAttachResponse"),
		NULL,
		CFNotificationSuspensionBehaviorDeliverImmediately);
}

SkypeDelegate *
GetSkypeDelegate(void)
{
	return delegate;
}

void
RemoveSkypeDelegate(void)
{
	if (delegate && delegate->clientApplicationName)
	{
		CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();
			
		CFNotificationCenterRemoveObserver(
			center,
			delegate->clientApplicationName,
			CFSTR("SKSkypeAPINotification"),
			NULL);

		CFNotificationCenterRemoveObserver(
			center,
			delegate->clientApplicationName,
			CFSTR("SKSkypeWillQuit"),
			NULL);

		CFNotificationCenterRemoveObserver(
			center,
			delegate->clientApplicationName,
			CFSTR("SKSkypeBecameAvailable"),
			NULL);

		CFNotificationCenterRemoveObserver(
			center,
			delegate->clientApplicationName,
			CFSTR("SKAvailabilityUpdate"),
			NULL);

		CFNotificationCenterRemoveObserver(
			center,
			delegate->clientApplicationName,
			CFSTR("SKSkypeAttachResponse"),
			NULL);
	}
	delegate = NULL;
}

int
IsSkypeAvailable(void)
{
	//is skype available?
	isavailable = 0;
	CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();

	CFNotificationCenterPostNotification(
		center,
		CFSTR("SKSkypeAPIAvailabilityRequest"),
		NULL,
		NULL,
		TRUE);
	
	//Should only take 1 second or less to reply
	RunCurrentEventLoop(1);
	int avail = isavailable;
	isavailable = 0;
	return avail;
}

int
IsSkypeRunning(void)
{
	OSStatus status = noErr;
	ProcessSerialNumber psn = {kNoProcess, kNoProcess};
	unsigned int procNameLength = 32;
	unsigned char procName[procNameLength];
	unsigned int i = 0;
	ProcessInfoRec info;
	info.processInfoLength = sizeof(ProcessInfoRec);
	info.processName = procName;
	info.processAppSpec = NULL;
	pid_t pid = 0;
	
	while(status == noErr)
	{
		for(i = 0; i < procNameLength; i++)
			procName[i] = '\0';
		
		status = GetNextProcess(&psn);
		if (status == noErr)
		{
			if (GetProcessInformation(&psn, &info) == noErr)
			{
				//for some reason first character is poisioned
				if (strcmp((char *)&procName[1], "Skype") == 0)
				{
					if (GetProcessPID(&psn, &pid) == noErr)
					{
						return (int)pid;
					}
				}
			}
		}
	}
	return 0;
}

void
ConnectToSkype(void)
{
	if (!delegate || !delegate->clientApplicationName)
	{
		printf("Error: Delegate not set\n");
		return;
	}
	
	if (!IsSkypeAvailable())
	{
		printf("Error: Skype not available\n");
	}

	CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();
			
	//do the connect
	CFNotificationCenterPostNotification(
		center,
		CFSTR("SKSkypeAPIAttachRequest"),
		delegate->clientApplicationName,
		NULL,
		TRUE);
}

void SendSkypeCommand(CFStringRef command)
{
	if (delegate == NULL)
	{
		printf("Can't send message, no delegate set\n");
		return;
	}
	if (command == NULL)
		return;
	if (!client_id)
	{
		printf("Can't send message, not connected\n");
		return;
	}

	CFRetain(command);

	CFNumberRef id_number = CFNumberCreate(NULL, kCFNumberIntType, &client_id);
	CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();
	const void *keys[] = {(void *)CFSTR("SKYPE_API_COMMAND"), (void *)CFSTR("SKYPE_API_CLIENT_ID")};
	const void *values[] = {command, id_number};
	CFDictionaryRef userInfo = CFDictionaryCreate(NULL, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);	
	
	//send message
	CFNotificationCenterPostNotification(
		center,
		CFSTR("SKSkypeAPICommand"),
		NULL,
		userInfo,
		FALSE);
	
	CFRelease(command);
	CFRelease(id_number);
	CFRelease(userInfo);
}

void DisconnectFromSkype(void)
{
	CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();
		
	if (client_id)
	{
		CFNumberRef id_number = CFNumberCreate(NULL, kCFNumberIntType, &client_id);
		const void *keys[] = {(void *)CFSTR("SKYPE_API_CLIENT_ID")};
		const void *values[] = {id_number};
		CFDictionaryRef userInfo = CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);	
	
		//disconnect
		CFNotificationCenterPostNotification(
			center,
			CFSTR("SKSkypeAPIDetachRequest"),
			NULL,
			userInfo,
			FALSE);
			
		client_id = 0;
	}
}
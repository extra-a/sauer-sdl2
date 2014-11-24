#import <Cocoa/Cocoa.h>

@interface Launcher : NSObject {
@private
    NSString *dataPath, *userPath;
    NSString *command;
}
@end

extern int mymain(int, char**);

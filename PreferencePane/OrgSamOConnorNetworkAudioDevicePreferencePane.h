/* OrgSamOConnorNetworkAudioDevicePreferencePane */

#import <Cocoa/Cocoa.h>
#import <PreferencePanes/NSPreferencePane.h>
#import <CoreFoundation/CoreFoundation.h>

@interface OrgSamOConnorNetworkAudioDevicePreferencePane : NSPreferencePane
{
    IBOutlet NSButton *enableNAD;
    IBOutlet NSTextField *NADhost;
    IBOutlet NSTextField *NADport;
    CFStringRef appID;
}
- (IBAction)enableNADclicked:(id)sender;
- (IBAction)testNAD:(id)sender;
@end

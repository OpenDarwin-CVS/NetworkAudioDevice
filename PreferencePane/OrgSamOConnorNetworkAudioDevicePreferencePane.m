#import "OrgSamOConnorNetworkAudioDevicePreferencePane.h"

@implementation OrgSamOConnorNetworkAudioDevicePreferencePane

- (IBAction)enableNADclicked:(id)sender
{
}

- (IBAction)testNAD:(id)sender
{
}

- (id)initWIthBundle:(NSBundle *)bundle
{
    if ( ( self = [super initWithBundle:bundle] ) != nil ) {
        appID = CFSTR("org.samoconnor.preferences.NetworkAudioDevice");
    }
    
    return self;
}

- (void)mainViewDidLoad
{
    [enableNAD setState:YES];
    [NADhost setStringValue:@"10.0.1.103"];
    [NADport setStringValue:@"16001"];
}

 


@end

#include "MacOSCamera.hpp"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <mutex>

// ------------------------------------------------------------------------------------------------------
// AVFoundation webcam capture. The session pushes sample buffers on a serial queue; the delegate converts
// the newest frame BGRA -> RGBA and stashes it under a mutex. CameraGetFrame() hands out the latest frame
// and clears the "new" flag so callers can skip redundant GPU uploads.
// ------------------------------------------------------------------------------------------------------

namespace
{
    struct CameraState
    {
        std::mutex           mutex;
        std::vector<uint8_t> frame; // RGBA8, width*height*4
        int                  width   = 0;
        int                  height  = 0;
        bool                 hasNew  = false;
        bool                 running = false;
    };
} // namespace

@interface DECameraDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
{
@public
    CameraState* state;
}
@end

@implementation DECameraDelegate
- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection
{
    (void)output;
    (void)connection;
    if ( state == nullptr )
        return;

    CVImageBufferRef img = CMSampleBufferGetImageBuffer( sampleBuffer );
    if ( img == nullptr )
        return;

    CVPixelBufferLockBaseAddress( img, kCVPixelBufferLock_ReadOnly );
    const int      w      = (int)CVPixelBufferGetWidth( img );
    const int      h      = (int)CVPixelBufferGetHeight( img );
    const size_t   stride = CVPixelBufferGetBytesPerRow( img );
    const uint8_t* base   = (const uint8_t*)CVPixelBufferGetBaseAddress( img );

    if ( base != nullptr && w > 0 && h > 0 )
    {
        std::vector<uint8_t> rgba( (size_t)w * h * 4 );
        for ( int y = 0; y < h; ++y )
        {
            const uint8_t* row = base + (size_t)y * stride;
            uint8_t*       dst = rgba.data() + (size_t)y * w * 4;
            for ( int x = 0; x < w; ++x )
            {
                // Capture format is 32BGRA.
                dst[x * 4 + 0] = row[x * 4 + 2]; // R <- B
                dst[x * 4 + 1] = row[x * 4 + 1]; // G
                dst[x * 4 + 2] = row[x * 4 + 0]; // B <- R
                dst[x * 4 + 3] = 255;
            }
        }

        std::lock_guard<std::mutex> lk( state->mutex );
        state->frame.swap( rgba );
        state->width  = w;
        state->height = h;
        state->hasNew = true;
    }

    CVPixelBufferUnlockBaseAddress( img, kCVPixelBufferLock_ReadOnly );
}
@end

namespace
{
    struct MacCamera
    {
        AVCaptureSession*        session  = nil;
        AVCaptureVideoDataOutput* output  = nil;
        DECameraDelegate*        delegate = nil;
        dispatch_queue_t         queue    = nullptr;
        CameraState              state;
    };
} // namespace

namespace Common::Utils::Mac
{
    void* CameraCreate()
    {
        return new MacCamera();
    }

    void CameraDestroy( void* handle )
    {
        auto* cam = static_cast<MacCamera*>( handle );
        if ( cam == nullptr )
            return;
        CameraStop( handle );
        @autoreleasepool
        {
            cam->session  = nil;
            cam->output   = nil;
            cam->delegate = nil;
        }
        delete cam;
    }

    bool CameraStart( void* handle )
    {
        auto* cam = static_cast<MacCamera*>( handle );
        if ( cam == nullptr )
            return false;
        if ( cam->state.running )
            return true;

        @autoreleasepool
        {
            // Ask for permission (no-op if already granted); frames start flowing once the user allows it.
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^( BOOL ){}];

            AVCaptureDevice* device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
            if ( device == nil )
                return false;

            NSError*             err   = nil;
            AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&err];
            if ( input == nil )
                return false;

            cam->session = [[AVCaptureSession alloc] init];
            if ( [cam->session canSetSessionPreset:AVCaptureSessionPreset1280x720] )
                cam->session.sessionPreset = AVCaptureSessionPreset1280x720;

            if ( [cam->session canAddInput:input] )
                [cam->session addInput:input];
            else
                return false;

            cam->delegate        = [[DECameraDelegate alloc] init];
            cam->delegate->state = &cam->state;

            cam->output = [[AVCaptureVideoDataOutput alloc] init];
            cam->output.videoSettings =
                 @{ (NSString*)kCVPixelBufferPixelFormatTypeKey : @( kCVPixelFormatType_32BGRA ) };
            cam->output.alwaysDiscardsLateVideoFrames = YES;

            cam->queue = dispatch_queue_create( "com.desert.camera", DISPATCH_QUEUE_SERIAL );
            [cam->output setSampleBufferDelegate:cam->delegate queue:cam->queue];

            if ( [cam->session canAddOutput:cam->output] )
                [cam->session addOutput:cam->output];
            else
                return false;

            [cam->session startRunning];
            cam->state.running = true;
        }
        return true;
    }

    void CameraStop( void* handle )
    {
        auto* cam = static_cast<MacCamera*>( handle );
        if ( cam == nullptr || !cam->state.running )
            return;
        @autoreleasepool
        {
            [cam->session stopRunning];
        }
        cam->state.running = false;
    }

    bool CameraIsRunning( void* handle )
    {
        auto* cam = static_cast<MacCamera*>( handle );
        return cam != nullptr && cam->state.running;
    }

    bool CameraGetFrame( void* handle, std::vector<uint8_t>& out, int& width, int& height )
    {
        auto* cam = static_cast<MacCamera*>( handle );
        if ( cam == nullptr )
            return false;

        std::lock_guard<std::mutex> lk( cam->state.mutex );
        if ( !cam->state.hasNew || cam->state.frame.empty() )
            return false;
        out           = cam->state.frame;
        width         = cam->state.width;
        height        = cam->state.height;
        cam->state.hasNew = false;
        return true;
    }
} // namespace Common::Utils::Mac

#define __CL_ENABLE_EXCEPTIONS
#include <CL/cl.hpp>

#include <EGL/egl.h>

#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>

#include "common.hpp"

const char oclProgB2B[] = "// clBuffer to clBuffer";
const char oclProgI2B[] = "// clImage to clBuffer";
const char oclProgI2I[] = \
  "__constant sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST; \n" \
    "\n" \
    "__kernel void Laplacian( \n" \
    "        __read_only image2d_t imgIn, \n" \
    "        __write_only image2d_t imgOut \n" \
    "    ) { \n" \
    "  \n" \
    "    const int2 pos = {get_global_id(0), get_global_id(1)}; \n" \
    "  \n" \
    "    float4 sum = (float4) 0.0f; \n" \
    "    sum += read_imagef(imgIn, sampler, pos + (int2)(-1,0)); \n" \
    "    sum += read_imagef(imgIn, sampler, pos + (int2)(+1,0)); \n" \
    "    sum += read_imagef(imgIn, sampler, pos + (int2)(0,-1)); \n" \
    "    sum += read_imagef(imgIn, sampler, pos + (int2)(0,+1)); \n" \
    "    sum -= read_imagef(imgIn, sampler, pos) * 4; \n" \
    "  \n" \
    "    write_imagef(imgOut, pos, sum*10); \n" \
    "} \n";

void dumpCLinfo()
{
    LOGD("*** OpenCL info ***");
    try
    {
        std::vector<cl::Platform> platforms;
        cl::Platform::get(&platforms);
        LOGD("OpenCL info: Found %d OpenCL platforms", platforms.size());
        for (int i = 0; i < platforms.size(); ++i)
        {
            std::string name = platforms[i].getInfo<CL_PLATFORM_NAME>();
            std::string version = platforms[i].getInfo<CL_PLATFORM_VERSION>();
            std::string profile = platforms[i].getInfo<CL_PLATFORM_PROFILE>();
            std::string extensions = platforms[i].getInfo<CL_PLATFORM_EXTENSIONS>();
            LOGD( "OpenCL info: Platform[%d] = %s, ver = %s, prof = %s, ext = %s",
                  i, name.c_str(), version.c_str(), profile.c_str(), extensions.c_str() );
        }

        std::vector<cl::Device> devices;
        platforms[0].getDevices(CL_DEVICE_TYPE_ALL, &devices);

        for (int i = 0; i < devices.size(); ++i)
        {
            std::string name = devices[i].getInfo<CL_DEVICE_NAME>();
            std::string extensions = devices[i].getInfo<CL_DEVICE_EXTENSIONS>();
            cl_ulong type = devices[i].getInfo<CL_DEVICE_TYPE>();
            LOGD( "OpenCL info: Device[%d] = %s (%s), ext = %s",
                  i, name.c_str(), (type==CL_DEVICE_TYPE_GPU ? "GPU" : "CPU"), extensions.c_str() );
        }
    }
    catch(cl::Error& e)
    {
        LOGE( "OpenCL info: error while gathering OpenCL info: %s (%d)", e.what(), e.err() );
    }
    catch(std::exception& e)
    {
        LOGE( "OpenCL info: error while gathering OpenCL info: %s", e.what() );
    }
    catch(...)
    {
        LOGE( "OpenCL info: unknown error while gathering OpenCL info" );
    }
    LOGD("*******************");
}

cl::Context theContext;
cl::CommandQueue theQueue;
cl::Program theProgB2B, theProgI2B, theProgI2I;

void initCL()
{
    dumpCLinfo();

    EGLDisplay mEglDisplay = eglGetCurrentDisplay();
    if (mEglDisplay == EGL_NO_DISPLAY)
        LOGE("initCL: eglGetCurrentDisplay() returned 'EGL_NO_DISPLAY', error = %x", eglGetError());

    EGLContext mEglContext = eglGetCurrentContext();
    if (mEglContext == EGL_NO_CONTEXT)
        LOGE("initCL: eglGetCurrentContext() returned 'EGL_NO_CONTEXT', error = %x", eglGetError());

    cl_context_properties props[] =
    {   CL_GL_CONTEXT_KHR,   (cl_context_properties) mEglContext,
        CL_EGL_DISPLAY_KHR,  (cl_context_properties) mEglDisplay,
        CL_CONTEXT_PLATFORM, 0,
        0 };

    try
    {
        cl::Platform p = cl::Platform::getDefault();
        std::string ext = p.getInfo<CL_PLATFORM_EXTENSIONS>();
        if(ext.find("cl_khr_gl_sharing") == std::string::npos)
            LOGE("Warning: CL-GL sharing isn't supported by PLATFORM");
        props[5] = (cl_context_properties) p();

        theContext = cl::Context(CL_DEVICE_TYPE_GPU, props);
        std::vector<cl::Device> devs = theContext.getInfo<CL_CONTEXT_DEVICES>();
        LOGD("Context returned %d devices, taking the 1st one", devs.size());
        ext = devs[0].getInfo<CL_DEVICE_EXTENSIONS>();
        if(ext.find("cl_khr_gl_sharing") == std::string::npos)
            LOGE("Warning: CL-GL sharing isn't supported by DEVICE");

        theQueue = cl::CommandQueue(theContext, devs[0]);

        cl::Program::Sources src(1, std::make_pair(oclProgI2I, sizeof(oclProgI2I)));
        theProgI2I = cl::Program(theContext, src);
        theProgI2I.build(devs);

        cv::ocl::attachContext(p.getInfo<CL_PLATFORM_NAME>(), p(), theContext(), devs[0]());
        if( cv::ocl::useOpenCL() )
            LOGD("OpenCV+OpenCL works OK!");
        else
            LOGE("Can't init OpenCV with OpenCL TAPI");
    }
    catch(cl::Error& e)
    {
        LOGE("cl::Error: %s (%d)", e.what(), e.err());
    }
    catch(std::exception& e)
    {
        LOGE("std::exception: %s", e.what());
    }
    catch(...)
    {
        LOGE( "OpenCL info: unknown error while initializing OpenCL stuff" );
    }
    LOGD("initCL completed");
}

void closeCL()
{
}

#define GL_TEXTURE_2D 0x0DE1
void procOCL_I2I(int texIn, int texOut, int w, int h)
{
    LOGD("procOCL_I2I(%d, %d, %d, %d)", texIn, texOut, w, h);
    cl::ImageGL imgIn (theContext, CL_MEM_READ_ONLY,  GL_TEXTURE_2D, 0, texIn);
    cl::ImageGL imgOut(theContext, CL_MEM_WRITE_ONLY, GL_TEXTURE_2D, 0, texOut);
    std::vector < cl::Memory > images;
    images.push_back(imgIn);
    images.push_back(imgOut);

    int64_t t = getTimeMs();
    theQueue.enqueueAcquireGLObjects(&images);
    theQueue.finish();
    LOGD("enqueueAcquireGLObjects() costs %d ms", getTimeInterval(t));

    t = getTimeMs();
    cl::Kernel Laplacian(theProgI2I, "Laplacian"); //TODO: may be done once
    Laplacian.setArg(0, imgIn);
    Laplacian.setArg(1, imgOut);
    theQueue.finish();
    LOGD("Kernel() costs %d ms", getTimeInterval(t));

    t = getTimeMs();
    theQueue.enqueueNDRangeKernel(Laplacian, cl::NullRange, cl::NDRange(w, h), cl::NullRange);
    theQueue.finish();
    LOGD("enqueueNDRangeKernel() costs %d ms", getTimeInterval(t));

    t = getTimeMs();
    theQueue.enqueueReleaseGLObjects(&images);
    theQueue.finish();
    LOGD("enqueueReleaseGLObjects() costs %d ms", getTimeInterval(t));
}

void procOCL_OCV(int tex, int w, int h)
{
    int64_t t = getTimeMs();
    cl::ImageGL imgIn (theContext, CL_MEM_READ_ONLY,  GL_TEXTURE_2D, 0, tex);
    std::vector < cl::Memory > images(1, imgIn);
    theQueue.enqueueAcquireGLObjects(&images);
    theQueue.finish();
    cv::UMat uIn, uOut, uTmp;
    cv::ocl::convertFromImage(imgIn(), uIn);
    LOGD("loading texture data to OpenCV UMat costs %d ms", getTimeInterval(t));
    theQueue.enqueueReleaseGLObjects(&images);

    t = getTimeMs();
    //cv::blur(uIn, uOut, cv::Size(5, 5));
    cv::Laplacian(uIn, uTmp, CV_8U);
    cv:multiply(uTmp, 10, uOut);
    cv::ocl::finish();
    LOGD("OpenCV processing costs %d ms", getTimeInterval(t));

    t = getTimeMs();
    cl::ImageGL imgOut(theContext, CL_MEM_WRITE_ONLY, GL_TEXTURE_2D, 0, tex);
    images.clear();
    images.push_back(imgOut);
    theQueue.enqueueAcquireGLObjects(&images);
    cl_mem clBuffer = (cl_mem)uOut.handle(cv::ACCESS_READ);
    cl_command_queue q = (cl_command_queue)cv::ocl::Queue::getDefault().ptr();
    size_t offset = 0;
    size_t origin[3] = { 0, 0, 0 };
    size_t region[3] = { w, h, 1 };
    CV_Assert(clEnqueueCopyBufferToImage (q, clBuffer, imgOut(), offset, origin, region, 0, NULL, NULL) == CL_SUCCESS);
    theQueue.enqueueReleaseGLObjects(&images);
    cv::ocl::finish();
    LOGD("uploading results to texture costs %d ms", getTimeInterval(t));
}
//
// Copyright (c) 2017 The Khronos Group Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include "crc32.h"
#include "kernelHelpers.h"
#include "errorHelpers.h"
#include "imageHelpers.h"
#include "typeWrappers.h"
#include "testHarness.h"
#include "parseParameters.h"

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>

#if defined(__MINGW32__)
#include "mingw_compat.h"
#endif

#if defined(_WIN32)
std::string slash = "\\";
#else
std::string slash = "/";
#endif

std::string get_file_name(const std::string &baseName, int index, const std::string &extension)
{
    std::ostringstream fileName;
    fileName << baseName << "." << index << extension;
    return fileName.str();
}

long get_file_size(const std::string &fileName)
{
    std::ifstream ifs(fileName.c_str(), std::ios::binary);
    if (!ifs.good())
        return 0;
    // get length of file:
    ifs.seekg(0, std::ios::end);
    std::ios::pos_type length = ifs.tellg();
    return static_cast<long>(length);
}

std::vector<char> get_file_content(const std::string &fileName)
{
    std::ifstream ifs(fileName.c_str(), std::ios::binary);
    if (!ifs.good())
        return std::vector<char>(0);
    // get length of file:
    ifs.seekg(0, std::ios::end);
    std::ios::pos_type length = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    // allocate memory:
    std::vector<char> content(static_cast<size_t>(length));

    // read data as a block:
    ifs.read(&content[0], length);
    return content;
}

std::string get_kernel_name(const std::string &source)
{
    // Count CRC
    cl_uint crc = crc32(source.data(), source.size());

    // Create list of kernel names
    std::string kernelsList;
    size_t kPos = source.find("kernel");
    while (kPos != std::string::npos)
    {
        // check for '__kernel'
        size_t pos = kPos;
        if (pos >= 2 && source[pos - 1] == '_' && source[pos - 2] == '_')
            pos -= 2;

        //check character before 'kernel' (white space expected)
        size_t wsPos = source.find_last_of(" \t\r\n", pos);
        if (wsPos == std::string::npos || wsPos + 1 == pos)
        {
            //check character after 'kernel' (white space expected)
            size_t akPos = kPos + sizeof("kernel") - 1;
            wsPos = source.find_first_of(" \t\r\n", akPos);
            if (!(wsPos == akPos))
            {
                kPos = source.find("kernel", kPos + 1);
                continue;
            }

            bool attributeFound;
            do
            {
                attributeFound = false;
                // find '(' after kernel name name
                size_t pPos = source.find("(", akPos);
                if (!(pPos != std::string::npos))
                    continue;

                // check for not empty kernel name before '('
                pos = source.find_last_not_of(" \t\r\n", pPos - 1);
                if (!(pos != std::string::npos && pos > akPos))
                    continue;

                //find character before kernel name
                wsPos = source.find_last_of(" \t\r\n", pos);
                if (!(wsPos != std::string::npos && wsPos >= akPos))
                    continue;

                std::string name = source.substr(wsPos + 1, pos + 1 - (wsPos + 1));
                //check for kernel attribute
                if (name == "__attribute__")
                {
                    attributeFound = true;
                    int pCount = 1;
                    akPos = pPos + 1;
                    while (pCount > 0 && akPos != std::string::npos)
                    {
                        akPos = source.find_first_of("()", akPos + 1);
                        if (akPos != std::string::npos)
                        {
                            if (source[akPos] == '(')
                                pCount++;
                            else
                                pCount--;
                        }
                    }
                }
                else
                {
                    kernelsList += name + ".";
                }
            } while (attributeFound);
        }
        kPos = source.find("kernel", kPos + 1);
    }
    std::ostringstream oss;
    if (MAX_LEN_FOR_KERNEL_LIST > 0)
    {
        if (kernelsList.size() > MAX_LEN_FOR_KERNEL_LIST + 1)
        {
            kernelsList = kernelsList.substr(0, MAX_LEN_FOR_KERNEL_LIST + 1);
            kernelsList[kernelsList.size() - 1] = '.';
            kernelsList[kernelsList.size() - 1] = '.';
        }
        oss << kernelsList;
    }
    oss << std::hex << std::setfill('0') << std::setw(8) << crc;
    return oss.str();
}

std::string add_build_options(const std::string &baseName, const char *options)
{
    if (options == 0 || options[0] == 0)
        return get_file_name(baseName, 0, "");

    bool equal = false;
    int i = 0;

    do
    {
        i++;
        std::string fileName = gSpirVPath + slash + get_file_name(baseName, i, ".options");
        long fileSize = get_file_size(fileName);
        if (fileSize == 0)
            break;
        //if(fileSize == strlen(options))
        {
            std::vector<char> options2 = get_file_content(fileName);
            options2.push_back(0); //terminate string
            equal = strcmp(options, &options2[0]) == 0;
        }
    } while (!equal);
    if (equal)
        return get_file_name(baseName, i, "");

    std::string fileName = gSpirVPath + slash + get_file_name(baseName, i, ".options");
    std::ofstream ofs(fileName.c_str(), std::ios::binary);
    if (!ofs.good())
    {
        log_info("OfflineCompiler: can't create options: %s\n", fileName.c_str());
        return "";
    }
    // write data as a block:
    ofs.write(options, strlen(options));
    log_info("OfflineCompiler: options added: %s\n", fileName.c_str());
    return get_file_name(baseName, i, "");
}

int create_single_kernel_helper_create_program(cl_context context,
                                               cl_program *outProgram,
                                               unsigned int numKernelLines,
                                               const char **kernelProgram,
                                               const char *buildOptions,
                                               const bool openclCXX)
{
    int error = CL_SUCCESS;
    std::string modifiedKernelStr;
    const char* modifiedKernelCode;

    if (gOfflineCompiler)
    {
        #ifndef CL_OFFLINE_COMPILER
            log_error("Offline compilation is not possible: CL_OFFLINE_COMPILER was not defined.\n");
            return -1;
        #endif // !CL_OFFLINE_COMPILER

        std::string kernel;
        for (size_t i = 0; i < numKernelLines; ++i)
        {
            std::string chunk(kernelProgram[i], 0, std::string::npos);
            kernel += chunk;
        }

        std::string kernelName = get_kernel_name(kernel);

        // set build options
        std::string bOptions;
        bOptions += buildOptions ? std::string(buildOptions) : "";

        kernelName = add_build_options(kernelName, buildOptions);

        std::string gOfflineCompilerInput = gSpirVPath + slash + kernelName + ".cl";
        std::string gOfflineCompilerOutput = gSpirVPath + slash + kernelName;

        // Get device CL_DEVICE_ADDRESS_BITS
        cl_uint device_address_space_size = 0;
        if (gOfflineCompilerOutputType == kSpir_v)
        {
            cl_device_id device;
            cl_uint numDevices = 0;
            cl_int error = clGetContextInfo(context, CL_CONTEXT_NUM_DEVICES, sizeof(cl_uint), &numDevices, 0);
            if (error != CL_SUCCESS)
            {
                print_error(error, "clGetContextInfo failed");
                return error;
            }

            std::vector<cl_device_id> devices(numDevices, 0);
            error = clGetContextInfo(context, CL_CONTEXT_DEVICES, numDevices*sizeof(cl_device_id), &devices[0], 0);
            if (error != CL_SUCCESS)
            {
                print_error(error, "clGetContextInfo failed");
                return error;
            }

            error = clGetContextInfo(context, CL_CONTEXT_DEVICES, numDevices*sizeof(cl_device_id), &devices[0], NULL);
            if (error != CL_SUCCESS)
            {
                print_error(error, "clGetContextInfo failed");
                return error;
            }

            if ((0 == device_address_space_size) && ((error = clGetDeviceInfo(devices[0], CL_DEVICE_ADDRESS_BITS, sizeof(cl_uint), &device_address_space_size, NULL))))
            {
                print_error(error, "Unable to obtain device address bits");
                return -1;
            }

            if (device_address_space_size == 32)
            {
                gOfflineCompilerOutput += ".spv32";
            }
            else if (device_address_space_size == 64)
            {
                gOfflineCompilerOutput += ".spv64";
            }
        }

        // try to read cached output file when test is run with gForceSpirVGenerate = false
        std::ifstream ifs(gOfflineCompilerOutput.c_str(), std::ios::binary);
        if (!ifs.good() || gForceSpirVGenerate)
        {
            if (gForceSpirVCache)
            {
                log_info("OfflineCompiler: can't open cached SpirV file: %s\n", gOfflineCompilerOutput.c_str());
                return -1;
            }

            ifs.close();

            if (!gForceSpirVGenerate)
                log_info("OfflineCompiler: can't find cached SpirV file: %s\n", gOfflineCompilerOutput.c_str());

            std::ofstream ofs(gOfflineCompilerInput.c_str(), std::ios::binary);
            if (!ofs.good())
            {
                log_info("OfflineCompiler: can't create source file: %s\n", gOfflineCompilerInput.c_str());
                return -1;
            }

            // write source to input file
            ofs.write(kernel.c_str(), kernel.size());
            ofs.close();

            // Set compiler options
            // Emit SPIR-V
            std::string compilerOptions = " -cc1 -emit-spirv";
            // <triple>: for 32 bit SPIR-V use spir-unknown-unknown, for 64 bit SPIR-V use spir64-unknown-unknown.
            if(device_address_space_size == 32)
            {
                compilerOptions += " -triple=spir-unknown-unknown";
            }
            else
            {
                compilerOptions += " -triple=spir64-unknown-unknown";
            }
            // Set OpenCL C++ flag required by SPIR-V-ready clang (compiler provided by Khronos)
            if(openclCXX)
            {
                compilerOptions = compilerOptions + " -cl-std=c++";
            }
            // Set correct includes
            if(openclCXX)
            {
                compilerOptions += " -I ";
                compilerOptions += STRINGIFY_VALUE(CL_LIBCLCXX_DIR);
            }
            else
            {
                compilerOptions += " -include opencl.h";
            }

            #ifdef CL_OFFLINE_COMPILER_OPTIONS
            compilerOptions += STRINGIFY_VALUE(CL_OFFLINE_COMPILER_OPTIONS);
            #endif

            // Add build options passed to this function
            compilerOptions += " " + bOptions;
            compilerOptions +=
                " " + gOfflineCompilerInput +
                " -o " + gOfflineCompilerOutput;
            std::string runString = STRINGIFY_VALUE(CL_OFFLINE_COMPILER) + compilerOptions;

            // execute script
            log_info("Executing command: %s\n", runString.c_str());
            fflush(stdout);
            int returnCode = system(runString.c_str());
            if (returnCode != 0)
            {
                log_error("ERROR: Command finished with error: 0x%x\n", returnCode);
                return CL_COMPILE_PROGRAM_FAILURE;
            }
            // read output file
            ifs.open(gOfflineCompilerOutput.c_str(), std::ios::binary);
            if (!ifs.good())
            {
                log_info("OfflineCompiler: can't read output file: %s\n", gOfflineCompilerOutput.c_str());
                return -1;
            }
        }

        // -----------------------------------------------------------------------------------
        // ------------- ONLY FOR OPENCL 22 CONFORMANCE TEST 22 DEVELOPMENT ------------------
        // -----------------------------------------------------------------------------------
        // Only OpenCL C++ to SPIR-V compilation
        #if defined(DEVELOPMENT) && defined(ONLY_SPIRV_COMPILATION)
        if(openclCXX)
        {
            return CL_SUCCESS;
        }
        #endif

        ifs.seekg(0, ifs.end);
        int length = ifs.tellg();
        ifs.seekg(0, ifs.beg);

        //treat modifiedProgram as input for clCreateProgramWithSource
        if (gOfflineCompilerOutputType == kSource)
        {
            // read source from file:
            std::vector<char> modifiedKernelBuf(length);

            ifs.read(&modifiedKernelBuf[0], length);
            ifs.close();

            for (int i = 0; i < length; i++)
                modifiedKernelStr.push_back(modifiedKernelBuf[i]);

            modifiedKernelCode = &modifiedKernelStr[0];

            /* Create the program object from source - to be removed in the future as we will use offline compiler here */
            *outProgram = clCreateProgramWithSource(context, numKernelLines, &modifiedKernelCode, NULL, &error);
            if (*outProgram == NULL || error != CL_SUCCESS)
            {
                print_error(error, "clCreateProgramWithSource failed");
                return error;
            }
        }
        //treat modifiedProgram as input for clCreateProgramWithBinary
        else if (gOfflineCompilerOutputType == kBinary)
        {
            // read binary from file:
            std::vector<unsigned char> modifiedKernelBuf(length);

            ifs.read((char *)&modifiedKernelBuf[0], length);
            ifs.close();

            cl_uint numDevices = 0;
            cl_int error = clGetContextInfo(context, CL_CONTEXT_NUM_DEVICES, sizeof(cl_uint), &numDevices, 0);
            test_error(error, "clGetContextInfo failed");

            std::vector<cl_device_id> devices(numDevices, 0);
            error = clGetContextInfo(context, CL_CONTEXT_DEVICES, numDevices*sizeof(cl_device_id), &devices[0], 0);
            test_error(error, "clGetContextInfo failed");

            size_t lengths = modifiedKernelBuf.size();
            const unsigned char *binaries = { &modifiedKernelBuf[0] };
            log_info("offlineCompiler: clCreateProgramWithSource replaced with clCreateProgramWithBinary\n");
            *outProgram = clCreateProgramWithBinary(context, 1, &devices[0], &lengths, &binaries, NULL, &error);
            if (*outProgram == NULL || error != CL_SUCCESS)
            {
                print_error(error, "clCreateProgramWithBinary failed");
                return error;
            }
        }
        //treat modifiedProgram as input for clCreateProgramWithIL
        else if (gOfflineCompilerOutputType == kSpir_v)
        {
            // read spir-v from file:
            std::vector<unsigned char> modifiedKernelBuf(length);

            ifs.read((char *)&modifiedKernelBuf[0], length);
            ifs.close();

            size_t length = modifiedKernelBuf.size();
            log_info("offlineCompiler: clCreateProgramWithSource replaced with clCreateProgramWithIL\n");

            *outProgram = clCreateProgramWithIL(context, &modifiedKernelBuf[0], length, &error);
            if (*outProgram == NULL || error != CL_SUCCESS)
            {
                print_error(error, "clCreateProgramWithIL failed");
                return error;
            }
        }
    }
    else // gOfflineCompiler == false
    {
        /* Create the program object from source */
        *outProgram = clCreateProgramWithSource(context, numKernelLines, kernelProgram, NULL, &error);
        if (*outProgram == NULL || error != CL_SUCCESS)
        {
            print_error(error, "clCreateProgramWithSource failed");
            return error;
        }
    }
    return 0;
}

int create_single_kernel_helper_with_build_options(cl_context context,
                                                   cl_program *outProgram,
                                                   cl_kernel *outKernel,
                                                   unsigned int numKernelLines,
                                                   const char **kernelProgram,
                                                   const char *kernelName,
                                                   const char *buildOptions,
                                                   const bool openclCXX)
{
    return create_single_kernel_helper(context, outProgram, outKernel, numKernelLines, kernelProgram, kernelName, buildOptions, openclCXX);
}

// Creates and builds OpenCL C/C++ program, and creates a kernel
int create_single_kernel_helper(cl_context context,
                                cl_program *outProgram,
                                cl_kernel *outKernel,
                                unsigned int numKernelLines,
                                const char **kernelProgram,
                                const char *kernelName,
                                const char *buildOptions,
                                const bool openclCXX)
{
    int error;
    // Create OpenCL C++ program
    if(openclCXX)
    {
    // -----------------------------------------------------------------------------------
    // ------------- ONLY FOR OPENCL 22 CONFORMANCE TEST 22 DEVELOPMENT ------------------
    // -----------------------------------------------------------------------------------
    // Only OpenCL C++ to SPIR-V compilation
    #if defined(DEVELOPMENT) && defined(ONLY_SPIRV_COMPILATION)
        // Save global variable
        bool tempgForceSpirVGenerate = gForceSpirVGenerate;
        // Force OpenCL C++ -> SPIR-V compilation on every run
        gForceSpirVGenerate = true;
    #endif
        error = create_openclcpp_program(
            context, outProgram, numKernelLines, kernelProgram, buildOptions
        );
        if (error != CL_SUCCESS)
        {
            log_error("Create program failed: %d, line: %d\n", error, __LINE__);
            return error;
        }
    // -----------------------------------------------------------------------------------
    // ------------- ONLY FOR OPENCL 22 CONFORMANCE TEST 22 DEVELOPMENT ------------------
    // -----------------------------------------------------------------------------------
    #if defined(DEVELOPMENT) && defined(ONLY_SPIRV_COMPILATION)
        // Restore global variables
        gForceSpirVGenerate = tempgForceSpirVGenerate;
        log_info("WARNING: KERNEL %s WAS ONLY COMPILED TO SPIR-V\n", kernelName);
        return error;
    #endif
    }
    // Create OpenCL C program
    else
    {
        error = create_single_kernel_helper_create_program(
            context, outProgram, numKernelLines, kernelProgram, buildOptions
        );
        if (error != CL_SUCCESS)
        {
            log_error("Create program failed: %d, line: %d\n", error, __LINE__);
            return error;
        }
    }
    // Remove offline-compiler-only build options
    std::string newBuildOptions;
    if (buildOptions != NULL)
    {
        newBuildOptions = buildOptions;
        std::string offlineCompierOptions[] = {
            "-cl-fp16-enable",
            "-cl-fp64-enable",
            "-cl-zero-init-local-mem-vars"
        };
        for(auto& s : offlineCompierOptions)
        {
            std::string::size_type i = newBuildOptions.find(s);
            if (i != std::string::npos)
                newBuildOptions.erase(i, s.length());
        }
    }
    // Build program and create kernel
    return build_program_create_kernel_helper(
        context, outProgram, outKernel, numKernelLines, kernelProgram, kernelName, newBuildOptions.c_str()
    );
}

// Creates OpenCL C++ program
int create_openclcpp_program(cl_context context,
                             cl_program *outProgram,
                             unsigned int numKernelLines,
                             const char **kernelProgram,
                             const char *buildOptions)
{
    // Save global variables
    bool tempgOfflineCompiler = gOfflineCompiler;
    OfflineCompilerOutputType tempgOfflineCompilerOutputType = gOfflineCompilerOutputType;
    // Force offline compilation to SPIR-V
    gOfflineCompiler = true;
    gOfflineCompilerOutputType = OfflineCompilerOutputType::kSpir_v;
    // Create program
    int error = create_single_kernel_helper_create_program(
        context, outProgram, numKernelLines, kernelProgram, buildOptions, true
    );
    // Restore global variable
    gOfflineCompiler = tempgOfflineCompiler;
    gOfflineCompilerOutputType = tempgOfflineCompilerOutputType;
    // Return result
    return error;
}

// Builds OpenCL C/C++ program and creates
int build_program_create_kernel_helper(cl_context context,
                                       cl_program *outProgram,
                                       cl_kernel *outKernel,
                                       unsigned int numKernelLines,
                                       const char **kernelProgram,
                                       const char *kernelName,
                                       const char *buildOptions)
{
    int error;
    /* Compile the program */
    int buildProgramFailed = 0;
    int printedSource = 0;
    error = clBuildProgram(*outProgram, 0, NULL, buildOptions, NULL, NULL);
    if (error != CL_SUCCESS)
    {
        unsigned int i;
        print_error(error, "clBuildProgram failed");
        buildProgramFailed = 1;
        printedSource = 1;
        log_error("Build options: %s\n", buildOptions);
        log_error("Original source is: ------------\n");
        for (i = 0; i < numKernelLines; i++)
            log_error("%s", kernelProgram[i]);
    }

    // Verify the build status on all devices
    cl_uint deviceCount = 0;
    error = clGetProgramInfo(*outProgram, CL_PROGRAM_NUM_DEVICES, sizeof(deviceCount), &deviceCount, NULL);
    if (error != CL_SUCCESS)
    {
        print_error(error, "clGetProgramInfo CL_PROGRAM_NUM_DEVICES failed");
        return error;
    }

    if (deviceCount == 0)
    {
        log_error("No devices found for program.\n");
        return -1;
    }

    cl_device_id *devices = (cl_device_id *)malloc(deviceCount * sizeof(cl_device_id));
    if (NULL == devices)
        return -1;
    BufferOwningPtr<cl_device_id> devicesBuf(devices);

    memset(devices, 0, deviceCount * sizeof(cl_device_id));
    error = clGetProgramInfo(*outProgram, CL_PROGRAM_DEVICES, sizeof(cl_device_id) * deviceCount, devices, NULL);
    if (error != CL_SUCCESS)
    {
        print_error(error, "clGetProgramInfo CL_PROGRAM_DEVICES failed");
        return error;
    }

    cl_uint z;
    bool buildFailed = false;
    for (z = 0; z < deviceCount; z++)
    {
        char deviceName[4096] = "";
        error = clGetDeviceInfo(devices[z], CL_DEVICE_NAME, sizeof(deviceName), deviceName, NULL);
        if (error != CL_SUCCESS || deviceName[0] == '\0')
        {
            log_error("Device \"%d\" failed to return a name\n", z);
            print_error(error, "clGetDeviceInfo CL_DEVICE_NAME failed");
        }

        cl_build_status buildStatus;
        error = clGetProgramBuildInfo(*outProgram, devices[z], CL_PROGRAM_BUILD_STATUS, sizeof(buildStatus), &buildStatus, NULL);
        if (error != CL_SUCCESS)
        {
            print_error(error, "clGetProgramBuildInfo CL_PROGRAM_BUILD_STATUS failed");
            return error;
        }

        if (buildStatus == CL_BUILD_SUCCESS && buildProgramFailed && deviceCount == 1)
        {
            buildFailed = true;
            log_error("clBuildProgram returned an error, but buildStatus is marked as CL_BUILD_SUCCESS.\n");
        }

        if (buildStatus != CL_BUILD_SUCCESS)
        {

            char statusString[64] = "";
            if (buildStatus == (cl_build_status)CL_BUILD_SUCCESS)
                sprintf(statusString, "CL_BUILD_SUCCESS");
            else if (buildStatus == (cl_build_status)CL_BUILD_NONE)
                sprintf(statusString, "CL_BUILD_NONE");
            else if (buildStatus == (cl_build_status)CL_BUILD_ERROR)
                sprintf(statusString, "CL_BUILD_ERROR");
            else if (buildStatus == (cl_build_status)CL_BUILD_IN_PROGRESS)
                sprintf(statusString, "CL_BUILD_IN_PROGRESS");
            else
                sprintf(statusString, "UNKNOWN (%d)", buildStatus);

            if (buildStatus != CL_BUILD_SUCCESS)
                log_error("Build not successful for device \"%s\", status: %s\n", deviceName, statusString);
            size_t paramSize = 0;
            error = clGetProgramBuildInfo(*outProgram, devices[z], CL_PROGRAM_BUILD_LOG, 0, NULL, &paramSize);
            if (error != CL_SUCCESS)
            {

                print_error(error, "clGetProgramBuildInfo CL_PROGRAM_BUILD_LOG failed");
                return error;
            }

            std::string log;
            log.resize(paramSize / sizeof(char));
            error = clGetProgramBuildInfo(*outProgram, devices[z], CL_PROGRAM_BUILD_LOG, paramSize, &log[0], NULL);
            if (error != CL_SUCCESS || log[0] == '\0')
            {
                log_error("Device %d (%s) failed to return a build log\n", z, deviceName);
                if (error)
                {
                    print_error(error, "clGetProgramBuildInfo CL_PROGRAM_BUILD_LOG failed");
                    return error;
                }
                else
                {
                    log_error("clGetProgramBuildInfo returned an empty log.\n");
                    return -1;
                }
            }
            // In this case we've already printed out the code above.
            if (!printedSource)
            {
                unsigned int i;
                log_error("Original source is: ------------\n");
                for (i = 0; i < numKernelLines; i++)
                    log_error("%s", kernelProgram[i]);
                printedSource = 1;
            }
            log_error("Build log for device \"%s\" is: ------------\n", deviceName);
            log_error("%s\n", log.c_str());
            log_error("\n----------\n");
            return -1;
        }
    }

    if (buildFailed)
    {
        return -1;
    }

    /* And create a kernel from it */
    if (kernelName != NULL)
    {
        *outKernel = clCreateKernel(*outProgram, kernelName, &error);
        if (*outKernel == NULL || error != CL_SUCCESS)
        {
            print_error(error, "Unable to create kernel");
            return error;
        }
    }

    return 0;
}

int get_device_version( cl_device_id id, size_t* major, size_t* minor)
{
    cl_char buffer[ 4098 ];
    size_t length;

    // Device version should fit the regex "OpenCL [0-9]+\.[0-9]+ *.*"
    cl_int error = clGetDeviceInfo( id, CL_DEVICE_VERSION, sizeof( buffer ), buffer, &length );
    test_error( error, "Unable to get device version string" );

    char *p1 = (char *)buffer + strlen( "OpenCL " );
    char *p2;
    while( *p1 == ' ' )
        p1++;
    *major = strtol( p1, &p2, 10 );
    error = *p2 != '.';
    test_error(error, "ERROR: Version number must contain a decimal point!");
    *minor = strtol( ++p2, NULL, 10 );
    return error;
}

int get_max_allowed_work_group_size( cl_context context, cl_kernel kernel, size_t *outMaxSize, size_t *outLimits )
{
    cl_device_id *devices;
    size_t size, maxCommonSize = 0;
    int numDevices, i, j, error;
  cl_uint numDims;
    size_t outSize;
  size_t sizeLimit[]={1,1,1};


    /* Assume fewer than 16 devices will be returned */
  error = clGetContextInfo( context, CL_CONTEXT_DEVICES, 0, NULL, &outSize );
  test_error( error, "Unable to obtain list of devices size for context" );
  devices = (cl_device_id *)malloc(outSize);
  BufferOwningPtr<cl_device_id> devicesBuf(devices);

  error = clGetContextInfo( context, CL_CONTEXT_DEVICES, outSize, devices, NULL );
  test_error( error, "Unable to obtain list of devices for context" );

    numDevices = (int)( outSize / sizeof( cl_device_id ) );

    for( i = 0; i < numDevices; i++ )
    {
        error = clGetDeviceInfo( devices[i], CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof( size ), &size, NULL );
        test_error( error, "Unable to obtain max work group size for device" );
        if( size < maxCommonSize || maxCommonSize == 0)
            maxCommonSize = size;

        error = clGetKernelWorkGroupInfo( kernel, devices[i], CL_KERNEL_WORK_GROUP_SIZE, sizeof( size ), &size, NULL );
        test_error( error, "Unable to obtain max work group size for device and kernel combo" );
        if( size < maxCommonSize  || maxCommonSize == 0)
            maxCommonSize = size;

    error= clGetDeviceInfo( devices[i], CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof( numDims ), &numDims, NULL);
    test_error( error, "clGetDeviceInfo failed for CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS");
    sizeLimit[0] = 1;
    error= clGetDeviceInfo( devices[i], CL_DEVICE_MAX_WORK_ITEM_SIZES, numDims*sizeof(size_t), sizeLimit, NULL);
        test_error( error, "clGetDeviceInfo failed for CL_DEVICE_MAX_WORK_ITEM_SIZES");

        if (outLimits != NULL)
        {
      if (i == 0) {
        for (j=0; j<3; j++)
          outLimits[j] = sizeLimit[j];
      } else {
        for (j=0; j<(int)numDims; j++) {
          if (sizeLimit[j] < outLimits[j])
            outLimits[j] = sizeLimit[j];
        }
      }
    }
    }

    *outMaxSize = (unsigned int)maxCommonSize;
    return 0;
}


int get_max_common_work_group_size( cl_context context, cl_kernel kernel,
                                   size_t globalThreadSize, size_t *outMaxSize )
{
  size_t sizeLimit[3];
    int error = get_max_allowed_work_group_size( context, kernel, outMaxSize, sizeLimit );
    if( error != 0 )
        return error;

    /* Now find the largest factor of globalThreadSize that is <= maxCommonSize */
    /* Note for speed, we don't need to check the range of maxCommonSize, b/c once it gets to 1,
     the modulo test will succeed and break the loop anyway */
    for( ; ( globalThreadSize % *outMaxSize ) != 0 || (*outMaxSize > sizeLimit[0]); (*outMaxSize)-- )
        ;
    return 0;
}

int get_max_common_2D_work_group_size( cl_context context, cl_kernel kernel,
                                   size_t *globalThreadSizes, size_t *outMaxSizes )
{
  size_t sizeLimit[3];
    size_t maxSize;
    int error = get_max_allowed_work_group_size( context, kernel, &maxSize, sizeLimit );
    if( error != 0 )
        return error;

    /* Now find a set of factors, multiplied together less than maxSize, but each a factor of the global
       sizes */

    /* Simple case */
    if( globalThreadSizes[ 0 ] * globalThreadSizes[ 1 ] <= maxSize )
    {
    if (globalThreadSizes[ 0 ] <= sizeLimit[0] &&  globalThreadSizes[ 1 ] <= sizeLimit[1]) {
      outMaxSizes[ 0 ] = globalThreadSizes[ 0 ];
      outMaxSizes[ 1 ] = globalThreadSizes[ 1 ];
      return 0;
    }
    }

  size_t remainingSize, sizeForThisOne;
  remainingSize = maxSize;
  int i, j;
  for (i=0 ; i<2; i++) {
    if (globalThreadSizes[i] > remainingSize)
      sizeForThisOne = remainingSize;
    else
      sizeForThisOne = globalThreadSizes[i];
    for (; (globalThreadSizes[i] % sizeForThisOne) != 0 || (sizeForThisOne > sizeLimit[i]); sizeForThisOne--) ;
    outMaxSizes[i] = sizeForThisOne;
    remainingSize = maxSize;
    for (j=0; j<=i; j++)
      remainingSize /=outMaxSizes[j];
  }

    return 0;
}

int get_max_common_3D_work_group_size( cl_context context, cl_kernel kernel,
                                      size_t *globalThreadSizes, size_t *outMaxSizes )
{
  size_t sizeLimit[3];
    size_t maxSize;
    int error = get_max_allowed_work_group_size( context, kernel, &maxSize, sizeLimit );
    if( error != 0 )
        return error;
    /* Now find a set of factors, multiplied together less than maxSize, but each a factor of the global
     sizes */

    /* Simple case */
    if( globalThreadSizes[ 0 ] * globalThreadSizes[ 1 ] * globalThreadSizes[ 2 ] <= maxSize )
    {
    if (globalThreadSizes[ 0 ] <= sizeLimit[0] && globalThreadSizes[ 1 ] <= sizeLimit[1] && globalThreadSizes[ 2 ] <= sizeLimit[2]) {
      outMaxSizes[ 0 ] = globalThreadSizes[ 0 ];
      outMaxSizes[ 1 ] = globalThreadSizes[ 1 ];
      outMaxSizes[ 2 ] = globalThreadSizes[ 2 ];
      return 0;
    }
    }

  size_t remainingSize, sizeForThisOne;
  remainingSize = maxSize;
  int i, j;
  for (i=0 ; i<3; i++) {
    if (globalThreadSizes[i] > remainingSize)
      sizeForThisOne = remainingSize;
    else
      sizeForThisOne = globalThreadSizes[i];
    for (; (globalThreadSizes[i] % sizeForThisOne) != 0 || (sizeForThisOne > sizeLimit[i]); sizeForThisOne--) ;
    outMaxSizes[i] = sizeForThisOne;
    remainingSize = maxSize;
    for (j=0; j<=i; j++)
      remainingSize /=outMaxSizes[j];
  }

    return 0;
}

/* Helper to determine if an extension is supported by a device */
int is_extension_available( cl_device_id device, const char *extensionName )
{
    char *extString;
    size_t size = 0;
    int err;
    int result = 0;

    if(( err = clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, 0, NULL, &size) ))
    {
        log_error( "Error: failed to determine size of device extensions string at %s:%d (err = %d)\n", __FILE__, __LINE__, err );
        return 0;
    }

    if( 0 == size )
        return 0;

    extString = (char*) malloc( size );

    if( NULL == extString )
    {
        log_error( "Error: unable to allocate %ld byte buffer for extension string at %s:%d (err = %d)\n", size, __FILE__, __LINE__,  err );
        return 0;
    }
    BufferOwningPtr<char> extStringBuf(extString);

    if(( err = clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, size, extString, NULL) ))
    {
        log_error( "Error: failed to obtain device extensions string at %s:%d (err = %d)\n", __FILE__, __LINE__, err );
        return 0;
    }

    if( strstr( extString, extensionName ) )
        result = 1;

    return result;
}

/* Helper to determine if a device supports an image format */
int is_image_format_supported( cl_context context, cl_mem_flags flags, cl_mem_object_type image_type, const cl_image_format *fmt )
{
    cl_image_format *list;
    cl_uint count = 0;
    cl_int err = clGetSupportedImageFormats( context, flags, image_type, 128, NULL, &count );
    if( count == 0 )
        return 0;

    list = (cl_image_format*) malloc( count * sizeof( cl_image_format ) );
    if( NULL == list )
    {
        log_error( "Error: unable to allocate %ld byte buffer for image format list at %s:%d (err = %d)\n", count * sizeof( cl_image_format ), __FILE__, __LINE__,  err );
        return 0;
    }
    BufferOwningPtr<cl_image_format> listBuf(list);


    cl_int error = clGetSupportedImageFormats( context, flags, image_type, count, list, NULL );
    if( error )
    {
        log_error( "Error: failed to obtain supported image type list at %s:%d (err = %d)\n", __FILE__, __LINE__, err );
        return 0;
    }

    // iterate looking for a match.
    cl_uint i;
    for( i = 0; i < count; i++ )
    {
        if( fmt->image_channel_data_type == list[ i ].image_channel_data_type &&
            fmt->image_channel_order == list[ i ].image_channel_order )
            break;
    }

    return ( i < count ) ? 1 : 0;
}

size_t get_pixel_bytes( const cl_image_format *fmt );
size_t get_pixel_bytes( const cl_image_format *fmt )
{
    size_t chanCount;
    switch( fmt->image_channel_order )
    {
        case CL_R:
        case CL_A:
        case CL_Rx:
        case CL_INTENSITY:
        case CL_LUMINANCE:
        case CL_DEPTH:
            chanCount = 1;
            break;
        case CL_RG:
        case CL_RA:
        case CL_RGx:
            chanCount = 2;
            break;
        case CL_RGB:
        case CL_RGBx:
        case CL_sRGB:
        case CL_sRGBx:
            chanCount = 3;
            break;
        case CL_RGBA:
        case CL_ARGB:
        case CL_BGRA:
        case CL_sBGRA:
        case CL_sRGBA:
#ifdef CL_1RGB_APPLE
        case CL_1RGB_APPLE:
#endif
#ifdef CL_BGR1_APPLE
        case CL_BGR1_APPLE:
#endif
            chanCount = 4;
            break;
        default:
            log_error("Unknown channel order at %s:%d!\n", __FILE__, __LINE__ );
            abort();
            break;
    }

    switch( fmt->image_channel_data_type )
    {
          case CL_UNORM_SHORT_565:
          case CL_UNORM_SHORT_555:
            return 2;

          case CL_UNORM_INT_101010:
            return 4;

          case CL_SNORM_INT8:
          case CL_UNORM_INT8:
          case CL_SIGNED_INT8:
          case CL_UNSIGNED_INT8:
            return chanCount;

          case CL_SNORM_INT16:
          case CL_UNORM_INT16:
          case CL_HALF_FLOAT:
          case CL_SIGNED_INT16:
          case CL_UNSIGNED_INT16:
#ifdef CL_SFIXED14_APPLE
          case CL_SFIXED14_APPLE:
#endif
            return chanCount * 2;

          case CL_SIGNED_INT32:
          case CL_UNSIGNED_INT32:
          case CL_FLOAT:
            return chanCount * 4;

        default:
            log_error("Unknown channel data type at %s:%d!\n", __FILE__, __LINE__ );
            abort();
    }

    return 0;
}

test_status verifyImageSupport( cl_device_id device )
{
    if( checkForImageSupport( device ) )
    {
        log_error( "ERROR: Device does not supported images as required by this test!\n" );
        return TEST_FAIL;
    }
    return TEST_PASS;
}

int checkForImageSupport( cl_device_id device )
{
    cl_uint i;
    int error;


    /* Check the device props to see if images are supported at all first */
    error = clGetDeviceInfo( device, CL_DEVICE_IMAGE_SUPPORT, sizeof( i ), &i, NULL );
    test_error( error, "Unable to query device for image support" );
    if( i == 0 )
    {
        return CL_IMAGE_FORMAT_NOT_SUPPORTED;
    }

    /* So our support is good */
    return 0;
}

int checkFor3DImageSupport( cl_device_id device )
{
    cl_uint i;
    int error;

    /* Check the device props to see if images are supported at all first */
    error = clGetDeviceInfo( device, CL_DEVICE_IMAGE_SUPPORT, sizeof( i ), &i, NULL );
    test_error( error, "Unable to query device for image support" );
    if( i == 0 )
    {
        return CL_IMAGE_FORMAT_NOT_SUPPORTED;
    }

    char profile[128];
    error = clGetDeviceInfo( device, CL_DEVICE_PROFILE, sizeof(profile ), profile, NULL );
    test_error( error, "Unable to query device for CL_DEVICE_PROFILE" );
    if( 0 == strcmp( profile, "EMBEDDED_PROFILE" ) )
    {
        size_t width = -1L;
        size_t height = -1L;
        size_t depth = -1L;
        error = clGetDeviceInfo( device, CL_DEVICE_IMAGE3D_MAX_WIDTH, sizeof(width), &width, NULL );
        test_error( error, "Unable to get CL_DEVICE_IMAGE3D_MAX_WIDTH" );
        error = clGetDeviceInfo( device, CL_DEVICE_IMAGE3D_MAX_HEIGHT, sizeof(height), &height, NULL );
        test_error( error, "Unable to get CL_DEVICE_IMAGE3D_MAX_HEIGHT" );
        error = clGetDeviceInfo( device, CL_DEVICE_IMAGE3D_MAX_DEPTH, sizeof(depth), &depth, NULL );
        test_error( error, "Unable to get CL_DEVICE_IMAGE3D_MAX_DEPTH" );

        if( 0 == (height | width | depth ))
            return CL_IMAGE_FORMAT_NOT_SUPPORTED;
    }

    /* So our support is good */
    return 0;
}

void * align_malloc(size_t size, size_t alignment)
{
#if defined(_WIN32) && defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
#elif  defined(__linux__) || defined (linux) || defined(__APPLE__)
    void * ptr = NULL;
    // alignemnt must be a power of two and multiple of sizeof(void *).
    if ( alignment < sizeof( void * ) )
    {
        alignment = sizeof( void * );
    }
#if defined(__ANDROID__)
    ptr = memalign(alignment, size);
    if ( ptr )
        return ptr;
#else
    if (0 == posix_memalign(&ptr, alignment, size))
        return ptr;
#endif
    return NULL;
#elif defined(__MINGW32__)
    return __mingw_aligned_malloc(size, alignment);
#else
    #error "Please add support OS for aligned malloc"
#endif
}

void   align_free(void * ptr)
{
#if defined(_WIN32) && defined(_MSC_VER)
    _aligned_free(ptr);
#elif  defined(__linux__) || defined (linux) || defined(__APPLE__)
    return  free(ptr);
#elif defined(__MINGW32__)
    return __mingw_aligned_free(ptr);
#else
    #error "Please add support OS for aligned free"
#endif
}

size_t get_min_alignment(cl_context context)
{
    static cl_uint align_size = 0;

    if( 0 == align_size )
    {
        cl_device_id * devices;
        size_t devices_size = 0;
        cl_uint result = 0;
        cl_int error;
        int i;

        error = clGetContextInfo (context,
                                  CL_CONTEXT_DEVICES,
                                  0,
                                  NULL,
                                  &devices_size);
        test_error_ret(error, "clGetContextInfo failed", 0);

        devices = (cl_device_id*)malloc(devices_size);
        if (devices == NULL) {
            print_error( error, "malloc failed" );
            return 0;
        }

        error = clGetContextInfo (context,
                                  CL_CONTEXT_DEVICES,
                                  devices_size,
                                  (void*)devices,
                                  NULL);
        test_error_ret(error, "clGetContextInfo failed", 0);

        for (i = 0; i < (int)(devices_size/sizeof(cl_device_id)); i++)
        {
            cl_uint alignment = 0;

            error = clGetDeviceInfo (devices[i],
                                     CL_DEVICE_MEM_BASE_ADDR_ALIGN,
                                     sizeof(cl_uint),
                                     (void*)&alignment,
                                     NULL);

            if (error == CL_SUCCESS)
            {
                alignment >>= 3;    // convert bits to bytes
                result = (alignment > result) ? alignment : result;
            }
            else
                print_error( error, "clGetDeviceInfo failed" );
        }

        align_size = result;
        free(devices);
    }

    return align_size;
}

cl_device_fp_config get_default_rounding_mode( cl_device_id device )
{
    char profileStr[128] = "";
    cl_device_fp_config single = 0;
    int error = clGetDeviceInfo( device, CL_DEVICE_SINGLE_FP_CONFIG, sizeof( single ), &single, NULL );
    if( error )
        test_error_ret( error, "Unable to get device CL_DEVICE_SINGLE_FP_CONFIG", 0 );

    if( single & CL_FP_ROUND_TO_NEAREST )
        return CL_FP_ROUND_TO_NEAREST;

    if( 0 == (single & CL_FP_ROUND_TO_ZERO) )
        test_error_ret( -1, "FAILURE: device must support either CL_DEVICE_SINGLE_FP_CONFIG or CL_FP_ROUND_TO_NEAREST", 0 );

    // Make sure we are an embedded device before allowing a pass
    if( (error = clGetDeviceInfo( device, CL_DEVICE_PROFILE, sizeof( profileStr ), &profileStr, NULL ) ))
        test_error_ret( error, "FAILURE: Unable to get CL_DEVICE_PROFILE", 0 );

    if( strcmp( profileStr, "EMBEDDED_PROFILE" ) )
        test_error_ret( error, "FAILURE: non-EMBEDDED_PROFILE devices must support CL_FP_ROUND_TO_NEAREST", 0 );

    return CL_FP_ROUND_TO_ZERO;
}

int checkDeviceForQueueSupport( cl_device_id device, cl_command_queue_properties prop )
{
    cl_command_queue_properties realProps;
    cl_int error = clGetDeviceInfo( device, CL_DEVICE_QUEUE_ON_HOST_PROPERTIES, sizeof( realProps ), &realProps, NULL );
    test_error_ret( error, "FAILURE: Unable to get device queue properties", 0 );

    return ( realProps & prop ) ? 1 : 0;
}

int printDeviceHeader( cl_device_id device )
{
    char deviceName[ 512 ], deviceVendor[ 512 ], deviceVersion[ 512 ], cLangVersion[ 512 ];
    int error;

    error = clGetDeviceInfo( device, CL_DEVICE_NAME, sizeof( deviceName ), deviceName, NULL );
    test_error( error, "Unable to get CL_DEVICE_NAME for device" );

    error = clGetDeviceInfo( device, CL_DEVICE_VENDOR, sizeof( deviceVendor ), deviceVendor, NULL );
    test_error( error, "Unable to get CL_DEVICE_VENDOR for device" );

    error = clGetDeviceInfo( device, CL_DEVICE_VERSION, sizeof( deviceVersion ), deviceVersion, NULL );
    test_error( error, "Unable to get CL_DEVICE_VERSION for device" );

    error = clGetDeviceInfo( device, CL_DEVICE_OPENCL_C_VERSION, sizeof( cLangVersion ), cLangVersion, NULL );
    test_error( error, "Unable to get CL_DEVICE_OPENCL_C_VERSION for device" );

    log_info("Compute Device Name = %s, Compute Device Vendor = %s, Compute Device Version = %s%s%s\n",
             deviceName, deviceVendor, deviceVersion, ( error == CL_SUCCESS ) ? ", CL C Version = " : "",
             ( error == CL_SUCCESS ) ? cLangVersion : "" );

    return CL_SUCCESS;
}

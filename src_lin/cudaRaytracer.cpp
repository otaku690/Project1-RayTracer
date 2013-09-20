#include <GL/glew.h>
#include <iostream>
#include <cuda_gl_interop.h>
#include "cudaRaytracer.h"
#include "cudaRaytracerKernel.h"


#include "timer.h"

CudaRayTracer::CudaRayTracer()
{
    h_pPrimitives = 0;
    h_pMaterials = 0;
    h_pLights = 0;

    numPrimitive = 0;
    numTriangle = 0;
    numSphere = 0;
    numLight = 0;
    numMaterial = 0;

    d_outputImage = 0;
    h_outputImage = 0;

    d_primitives = 0;
    d_lights = 0;
    d_materials = 0;
}

CudaRayTracer::~CudaRayTracer()
{
    cleanUp();
}

void CudaRayTracer::renderImage( cudaGraphicsResource* pboResource )
{


    cudaErrorCheck( cudaGraphicsMapResources( 1, &pboResource, 0 ) );
    cudaErrorCheck( cudaGraphicsResourceGetMappedPointer((void**) &d_outputImage, &pboSize, pboResource ) );
    cudaErrorCheck( cudaMemset( (void*)d_outputImage, 0, sizeof(unsigned char) * 4 * width * height ) );

    GpuTimer timer;
    timer.Start();
    //Launch the ray tracing kernel through the wrapper
    rayTracerKernelWrapper( d_outputImage, width, height, cameraData, 
                            d_primitives, numPrimitive, d_lights, numLight, d_materials, numMaterial );
    timer.Stop();

    cudaErrorCheck( cudaGraphicsUnmapResources( 1, &pboResource, 0 ) );


    
}

void CudaRayTracer::packSceneDescData( const SceneDesc &sceneDesc )
{
    //packing the camrea setting
    cameraData.eyePos = sceneDesc.eyePos;
    cameraData.viewportHalfDim.y = tan( sceneDesc.fovy / 2.0 );
    cameraData.viewportHalfDim.x = (float)sceneDesc.width / (float) sceneDesc.height * cameraData.viewportHalfDim.y;

    width = sceneDesc.width;
    height = sceneDesc.height;

    //construct the 3 orthoogonal vectors constitute a frame
    cameraData.uVec = glm::normalize( sceneDesc.up );
    cameraData.wVec = glm::normalize( sceneDesc.center - sceneDesc.eyePos );
    cameraData.vVec = glm::normalize( glm::cross( cameraData.wVec, cameraData.uVec ) );
    cameraData.uVec = glm::normalize( glm::cross( cameraData.vVec, cameraData.wVec ) );

    //packing the primitives
    numPrimitive = sceneDesc.primitives.size();
    h_pPrimitives = new _Primitive[ numPrimitive ]; 

    for( int i = 0; i < 65/*sceneDesc.primitives.size()*/; ++i )
    {
        if( sceneDesc.primitives[i]->toString().compare("sphere") == 0 )
        {
            h_pPrimitives[i].center = glm::vec3( ((Sphere*)sceneDesc.primitives[i])->center );
            h_pPrimitives[i].radius = ((Sphere*)sceneDesc.primitives[i])->radius;
            h_pPrimitives[i].type = 0; //sphere type
        }
        else if( sceneDesc.primitives[i]->toString().compare("triangle") == 0 )
        {
            for( int n = 0; n < 3; ++n )
                h_pPrimitives[i].vert[n]  =((Triangle*)sceneDesc.primitives[i])->v[n];

            for( int n = 0; n < 3; ++n )
                h_pPrimitives[i].normal[n] =((Triangle*)sceneDesc.primitives[i])->n[n];

            h_pPrimitives[i].pn = ((Triangle*)sceneDesc.primitives[i])->pn;

            h_pPrimitives[i].type = 1; //triangle type
        }
        h_pPrimitives[i].transform = sceneDesc.primitives[i]->transform;
        h_pPrimitives[i].invTrans = sceneDesc.primitives[i]->invTrans;
        h_pPrimitives[i].mtl_id = sceneDesc.primitives[i]->mtl_idx;
    }

    //pack light sources
    numLight = sceneDesc.lights.size();
    h_pLights = new _Light[numLight];
    for( int i = 0; i < numLight; i++ )
    {
        h_pLights[i].pos = sceneDesc.lights[i].pos;
        h_pLights[i].color = sceneDesc.lights[i].color;
        h_pLights[i].attenu_linear = sceneDesc.lights[i].attenu_linear;
        h_pLights[i].attenu_const = sceneDesc.lights[i].attenu_const;
        h_pLights[i].attenu_quadratic = sceneDesc.lights[i].attenu_quadratic;

        h_pLights[i].type = sceneDesc.lights[i].type;
        h_pLights[i].normal = sceneDesc.lights[i].normal;
        h_pLights[i].width = sceneDesc.lights[i].width;
    }

    //pack materails 
    numMaterial = sceneDesc.mtls.size();
    h_pMaterials = new _Material[ numMaterial ];
    for( int i = 0; i < numMaterial; ++i )
    {
        h_pMaterials[i].ambient = sceneDesc.mtls[i].ambient;
        h_pMaterials[i].emission = sceneDesc.mtls[i].emission;
        h_pMaterials[i].diffuse = sceneDesc.mtls[i].diffuse;
        h_pMaterials[i].specular = sceneDesc.mtls[i].specular;
        h_pMaterials[i].shininess = sceneDesc.mtls[i].shininess;
    }
}

void CudaRayTracer::cleanUp()
{
    if( h_pPrimitives )
        delete [] h_pPrimitives;
    h_pPrimitives  = 0;

    if( h_pMaterials )
        delete [] h_pMaterials;
    h_pMaterials = 0;

    if( h_pLights )
        delete[] h_pLights;
    h_pLights = 0;

    if( h_outputImage )
        delete [] h_outputImage;
    h_outputImage = 0;

    //if( d_outputImage )
    //    cudaErrorCheck( cudaFree( d_outputImage ) );
    //d_outputImage = 0;

    if( d_primitives )
        cudaErrorCheck( cudaFree( d_primitives  ) );
    d_primitives = 0;


    if( d_lights )
        cudaErrorCheck( cudaFree( d_lights ) );
    d_lights = 0;

    if( d_materials )
        cudaErrorCheck( cudaFree( d_materials ) );
    d_materials = 0;
}

void  CudaRayTracer::init( const SceneDesc &scene )
{
    if( scene.width < 1 || scene.height < 1 )
        return;

    //Pack scene description data
    packSceneDescData( scene );

    //allocate memory in the device
    cudaErrorCheck( cudaMalloc( &d_primitives, sizeof( _Primitive ) * numPrimitive ) );
    cudaErrorCheck( cudaMalloc( &d_lights, sizeof( _Light ) * numLight ) );
    cudaErrorCheck( cudaMalloc( &d_materials, sizeof( _Material ) * numMaterial ) );

    //Send scene description data to the device
    cudaErrorCheck( cudaMemcpy( (void*)d_primitives, h_pPrimitives, sizeof( _Primitive ) * numPrimitive, cudaMemcpyHostToDevice) );
    cudaErrorCheck( cudaMemcpy( (void*)d_lights, h_pLights, sizeof( _Light ) * numLight , cudaMemcpyHostToDevice ) );
    cudaErrorCheck( cudaMemcpy( (void*)d_materials, h_pMaterials, sizeof( _Material ) * numMaterial , cudaMemcpyHostToDevice ) );
}

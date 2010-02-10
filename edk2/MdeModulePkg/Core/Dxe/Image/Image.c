/** @file
  Core image handling services to load and unload PeImage.

Copyright (c) 2006 - 2009, Intel Corporation. <BR>
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "DxeMain.h"
#include "Image.h"

//
// Module Globals
//
LOADED_IMAGE_PRIVATE_DATA  *mCurrentImage = NULL;

LOAD_PE32_IMAGE_PRIVATE_DATA  mLoadPe32PrivateData = {
  LOAD_PE32_IMAGE_PRIVATE_DATA_SIGNATURE,
  NULL,
  {
    CoreLoadImageEx,
    CoreUnloadImageEx
  }
};


//
// This code is needed to build the Image handle for the DXE Core
//
LOADED_IMAGE_PRIVATE_DATA mCorePrivateImage  = {
  LOADED_IMAGE_PRIVATE_DATA_SIGNATURE,            // Signature
  NULL,                                           // Image handle
  EFI_IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER,    // Image type
  TRUE,                                           // If entrypoint has been called
  NULL, // EntryPoint
  {
    EFI_LOADED_IMAGE_INFORMATION_REVISION,        // Revision
    NULL,                                         // Parent handle
    NULL,                                         // System handle

    NULL,                                         // Device handle
    NULL,                                         // File path
    NULL,                                         // Reserved

    0,                                            // LoadOptionsSize
    NULL,                                         // LoadOptions

    NULL,                                         // ImageBase
    0,                                            // ImageSize
    EfiBootServicesCode,                          // ImageCodeType
    EfiBootServicesData                           // ImageDataType
  },
  (EFI_PHYSICAL_ADDRESS)0,    // ImageBasePage
  0,                          // NumberOfPages
  NULL,                       // FixupData
  0,                          // Tpl
  EFI_SUCCESS,                // Status
  0,                          // ExitDataSize
  NULL,                       // ExitData
  NULL,                       // JumpBuffer
  NULL,                       // JumpContext
  0,                          // Machine
  NULL,                       // Ebc
  NULL,                       // RuntimeData
  NULL                        // LoadedImageDevicePath
};
//
// The field is define for Loading modules at fixed address feature to tracker the PEI code
// memory range usage. It is a bit mapped array in which every bit indicates the correspoding memory page
// available or not. 
//
GLOBAL_REMOVE_IF_UNREFERENCED    UINT64                *mDxeCodeMemoryRangeUsageBitMap=NULL;

/**
  Add the Image Services to EFI Boot Services Table and install the protocol
  interfaces for this image.

  @param  HobStart                The HOB to initialize

  @return Status code.

**/
EFI_STATUS
CoreInitializeImageServices (
  IN  VOID *HobStart
  )
{
  EFI_STATUS                        Status;
  LOADED_IMAGE_PRIVATE_DATA         *Image;
  EFI_PHYSICAL_ADDRESS              DxeCoreImageBaseAddress;
  UINT64                            DxeCoreImageLength;
  VOID                              *DxeCoreEntryPoint;
  EFI_PEI_HOB_POINTERS              DxeCoreHob;
  //
  // Searching for image hob
  //
  DxeCoreHob.Raw          = HobStart;
  while ((DxeCoreHob.Raw = GetNextHob (EFI_HOB_TYPE_MEMORY_ALLOCATION, DxeCoreHob.Raw)) != NULL) {
    if (CompareGuid (&DxeCoreHob.MemoryAllocationModule->MemoryAllocationHeader.Name, &gEfiHobMemoryAllocModuleGuid)) {
      //
      // Find Dxe Core HOB
      //
      break;
    }
    DxeCoreHob.Raw = GET_NEXT_HOB (DxeCoreHob);
  }
  ASSERT (DxeCoreHob.Raw != NULL);

  DxeCoreImageBaseAddress = DxeCoreHob.MemoryAllocationModule->MemoryAllocationHeader.MemoryBaseAddress;
  DxeCoreImageLength      = DxeCoreHob.MemoryAllocationModule->MemoryAllocationHeader.MemoryLength;
  DxeCoreEntryPoint       = (VOID *) (UINTN) DxeCoreHob.MemoryAllocationModule->EntryPoint;
  gDxeCoreFileName        = &DxeCoreHob.MemoryAllocationModule->ModuleName;
  //
  // Initialize the fields for an internal driver
  //
  Image = &mCorePrivateImage;

  Image->EntryPoint         = (EFI_IMAGE_ENTRY_POINT)(UINTN)DxeCoreEntryPoint;
  Image->ImageBasePage      = DxeCoreImageBaseAddress;
  Image->NumberOfPages      = (UINTN)(EFI_SIZE_TO_PAGES((UINTN)(DxeCoreImageLength)));
  Image->Tpl                = gEfiCurrentTpl;
  Image->Info.SystemTable   = gDxeCoreST;
  Image->Info.ImageBase     = (VOID *)(UINTN)DxeCoreImageBaseAddress;
  Image->Info.ImageSize     = DxeCoreImageLength;

  //
  // Install the protocol interfaces for this image
  //
  Status = CoreInstallProtocolInterface (
             &Image->Handle,
             &gEfiLoadedImageProtocolGuid,
             EFI_NATIVE_INTERFACE,
             &Image->Info
             );
  ASSERT_EFI_ERROR (Status);

  mCurrentImage = Image;

  //
  // Fill in DXE globals
  //
  gDxeCoreImageHandle = Image->Handle;
  gDxeCoreLoadedImage = &Image->Info;

  if (FeaturePcdGet (PcdFrameworkCompatibilitySupport)) {
    //
    // Export DXE Core PE Loader functionality for backward compatibility.
    //
    Status = CoreInstallProtocolInterface (
      &mLoadPe32PrivateData.Handle,
      &gEfiLoadPeImageProtocolGuid,
      EFI_NATIVE_INTERFACE,
      &mLoadPe32PrivateData.Pe32Image
      );
  }

  return Status;
}

/**
  Read image file (specified by UserHandle) into user specified buffer with specified offset
  and length.

  @param  UserHandle             Image file handle
  @param  Offset                 Offset to the source file
  @param  ReadSize               For input, pointer of size to read; For output,
                                 pointer of size actually read.
  @param  Buffer                 Buffer to write into

  @retval EFI_SUCCESS            Successfully read the specified part of file
                                 into buffer.

**/
EFI_STATUS
EFIAPI
CoreReadImageFile (
  IN     VOID    *UserHandle,
  IN     UINTN   Offset,
  IN OUT UINTN   *ReadSize,
  OUT    VOID    *Buffer
  )
{
  UINTN               EndPosition;
  IMAGE_FILE_HANDLE  *FHand;

  FHand = (IMAGE_FILE_HANDLE  *)UserHandle;
  ASSERT (FHand->Signature == IMAGE_FILE_HANDLE_SIGNATURE);

  //
  // Move data from our local copy of the file
  //
  EndPosition = Offset + *ReadSize;
  if (EndPosition > FHand->SourceSize) {
    *ReadSize = (UINT32)(FHand->SourceSize - Offset);
  }
  if (Offset >= FHand->SourceSize) {
      *ReadSize = 0;
  }

  CopyMem (Buffer, (CHAR8 *)FHand->Source + Offset, *ReadSize);
  return EFI_SUCCESS;
}
/**
  To check memory usage bit map arry to figure out if the memory range the image will be loaded in is available or not. If 
  memory range is avaliable, the function will mark the correponding bits to 1 which indicates the memory range is used.
  The function is only invoked when load modules at fixed address feature is enabled. 
  
  @param  ImageBase                The base addres the image will be loaded at.
  @param  ImageSize                The size of the image
  
  @retval EFI_SUCCESS              The memory range the image will be loaded in is available
  @retval EFI_NOT_FOUND            The memory range the image will be loaded in is not available
**/
EFI_STATUS
CheckAndMarkFixLoadingMemoryUsageBitMap (
  IN  EFI_PHYSICAL_ADDRESS          ImageBase,
  IN  UINTN                         ImageSize
  )
{
   UINT32                             DxeCodePageNumber;
   UINT64                             DxeCodeSize; 
   EFI_PHYSICAL_ADDRESS               DxeCodeBase;
   UINTN                              BaseOffsetPageNumber;
   UINTN                              TopOffsetPageNumber;
   UINTN                              Index;
   //
   // The DXE code range includes RuntimeCodePage range and Boot time code range.
   //  
   DxeCodePageNumber = PcdGet32(PcdLoadFixAddressRuntimeCodePageNumber);
   DxeCodePageNumber += PcdGet32(PcdLoadFixAddressBootTimeCodePageNumber);
   DxeCodeSize       = EFI_PAGES_TO_SIZE(DxeCodePageNumber);
   DxeCodeBase       =  gLoadModuleAtFixAddressConfigurationTable.DxeCodeTopAddress - DxeCodeSize;
   
   //
   // If the memory usage bit map is not initialized,  do it. Every bit in the array 
   // indicate the status of the corresponding memory page, available or not
   // 
   if (mDxeCodeMemoryRangeUsageBitMap == NULL) {
     mDxeCodeMemoryRangeUsageBitMap = AllocateZeroPool(((DxeCodePageNumber/64) + 1)*sizeof(UINT64));
   }
   //
   // If the Dxe code memory range is not allocated or the bit map array allocation failed, return EFI_NOT_FOUND
   //
   if (!gLoadFixedAddressCodeMemoryReady || mDxeCodeMemoryRangeUsageBitMap == NULL) {
     return EFI_NOT_FOUND;
   }
   //
   // Test the memory range for loading the image in the DXE code range.
   //
   if (gLoadModuleAtFixAddressConfigurationTable.DxeCodeTopAddress <  ImageBase + ImageSize ||
       DxeCodeBase >  ImageBase) {
     return EFI_NOT_FOUND;   
   }   
   //
   // Test if the memory is avalaible or not.
   // 
   BaseOffsetPageNumber = (UINTN)EFI_SIZE_TO_PAGES((UINT32)(ImageBase - DxeCodeBase));
   TopOffsetPageNumber  = (UINTN)EFI_SIZE_TO_PAGES((UINT32)(ImageBase + ImageSize - DxeCodeBase));
   for (Index = BaseOffsetPageNumber; Index < TopOffsetPageNumber; Index ++) {
     if ((mDxeCodeMemoryRangeUsageBitMap[Index / 64] & LShiftU64(1, (Index % 64))) != 0) {
       //
       // This page is already used.
       //
       return EFI_NOT_FOUND;  
     }
   }
   
   //
   // Being here means the memory range is available.  So mark the bits for the memory range
   // 
   for (Index = BaseOffsetPageNumber; Index < TopOffsetPageNumber; Index ++) {
     mDxeCodeMemoryRangeUsageBitMap[Index / 64] |= LShiftU64(1, (Index % 64));
   }
   return  EFI_SUCCESS;   
}
/**

  Get the fixed loadding address from image header assigned by build tool. This function only be called
  when Loading module at Fixed address feature enabled.

  @param  ImageContext              Pointer to the image context structure that describes the PE/COFF
                                    image that needs to be examined by this function.
  @retval EFI_SUCCESS               An fixed loading address is assigned to this image by build tools .
  @retval EFI_NOT_FOUND             The image has no assigned fixed loadding address.

**/
EFI_STATUS
GetPeCoffImageFixLoadingAssignedAddress(
  IN OUT PE_COFF_LOADER_IMAGE_CONTEXT  *ImageContext
  )
{
   UINTN                              SectionHeaderOffset;
   EFI_STATUS                         Status;
   EFI_IMAGE_SECTION_HEADER           SectionHeader;
   EFI_IMAGE_OPTIONAL_HEADER_UNION    *ImgHdr;
   UINT16                             Index;
   UINTN                              Size;
   UINT16                             NumberOfSections;
   IMAGE_FILE_HANDLE                  *Handle;
   UINT64                             ValueInSectionHeader;
                             

   Status = EFI_NOT_FOUND;
 
   //
   // Get PeHeader pointer
   //
   Handle = (IMAGE_FILE_HANDLE*)ImageContext->Handle;
   ImgHdr = (EFI_IMAGE_OPTIONAL_HEADER_UNION *)((CHAR8* )Handle->Source + ImageContext->PeCoffHeaderOffset);
   SectionHeaderOffset = (UINTN)(
                                 ImageContext->PeCoffHeaderOffset +
                                 sizeof (UINT32) +
                                 sizeof (EFI_IMAGE_FILE_HEADER) +
                                 ImgHdr->Pe32.FileHeader.SizeOfOptionalHeader
                                 );
   NumberOfSections = ImgHdr->Pe32.FileHeader.NumberOfSections;

   //
   // Get base address from the first section header that doesn't point to code section.
   //
   for (Index = 0; Index < NumberOfSections; Index++) {
     //
     // Read section header from file
     //
     Size = sizeof (EFI_IMAGE_SECTION_HEADER);
     Status = ImageContext->ImageRead (
                              ImageContext->Handle,
                              SectionHeaderOffset,
                              &Size,
                              &SectionHeader
                              );
     if (EFI_ERROR (Status)) {
       return Status;
     }
     
     Status = EFI_NOT_FOUND;
     
     if ((SectionHeader.Characteristics & EFI_IMAGE_SCN_CNT_CODE) == 0) {
       //
       // Build tool will save the address in PointerToRelocations & PointerToLineNumbers fields in the first section header
       // that doesn't point to code section in image header, as well as ImageBase field of image header. And there is an 
       // assumption that when the feature is enabled, if a module is assigned a loading address by tools, PointerToRelocations  
       // & PointerToLineNumbers fields should NOT be Zero, or else, these 2 fileds should be set to Zero
       //
       ValueInSectionHeader = ReadUnaligned64((UINT64*)&SectionHeader.PointerToRelocations);
       if (ValueInSectionHeader != 0) {
         //
         // When the feature is configured as load module at fixed absolute address, the ImageAddress field of ImageContext 
         // hold the spcified address. If the feature is configured as load module at fixed offset, ImageAddress hold an offset
         // relative to top address
         //
         if ((INT64)FixedPcdGet64(PcdLoadModuleAtFixAddressEnable) < 0) {
         	 ImageContext->ImageAddress = gLoadModuleAtFixAddressConfigurationTable.DxeCodeTopAddress + (INT64)ImageContext->ImageAddress;
         }
         //
         // Check if the memory range is avaliable.
         //
         Status = CheckAndMarkFixLoadingMemoryUsageBitMap (ImageContext->ImageAddress, (UINTN)(ImageContext->ImageSize + ImageContext->SectionAlignment));
       }
       break; 
     }
     SectionHeaderOffset += sizeof (EFI_IMAGE_SECTION_HEADER);
   }
   DEBUG ((EFI_D_INFO|EFI_D_LOAD, "LOADING MODULE FIXED INFO: Loading module at fixed address %x. Status = %r \n", ImageContext->ImageAddress, Status));
   return Status;
}
/**
  Loads, relocates, and invokes a PE/COFF image

  @param  BootPolicy              If TRUE, indicates that the request originates
                                  from the boot manager, and that the boot
                                  manager is attempting to load FilePath as a
                                  boot selection.
  @param  Pe32Handle              The handle of PE32 image
  @param  Image                   PE image to be loaded
  @param  DstBuffer               The buffer to store the image
  @param  EntryPoint              A pointer to the entry point
  @param  Attribute               The bit mask of attributes to set for the load
                                  PE image

  @retval EFI_SUCCESS             The file was loaded, relocated, and invoked
  @retval EFI_OUT_OF_RESOURCES    There was not enough memory to load and
                                  relocate the PE/COFF file
  @retval EFI_INVALID_PARAMETER   Invalid parameter
  @retval EFI_BUFFER_TOO_SMALL    Buffer for image is too small

**/
EFI_STATUS
CoreLoadPeImage (
  IN BOOLEAN                     BootPolicy,
  IN VOID                        *Pe32Handle,
  IN LOADED_IMAGE_PRIVATE_DATA   *Image,
  IN EFI_PHYSICAL_ADDRESS        DstBuffer    OPTIONAL,
  OUT EFI_PHYSICAL_ADDRESS       *EntryPoint  OPTIONAL,
  IN  UINT32                     Attribute
  )
{
  EFI_STATUS                Status;
  BOOLEAN                   DstBufAlocated;
  UINTN                     Size;

  ZeroMem (&Image->ImageContext, sizeof (Image->ImageContext));

  Image->ImageContext.Handle    = Pe32Handle;
  Image->ImageContext.ImageRead = (PE_COFF_LOADER_READ_FILE)CoreReadImageFile;

  //
  // Get information about the image being loaded
  //
  Status = PeCoffLoaderGetImageInfo (&Image->ImageContext);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (!EFI_IMAGE_MACHINE_TYPE_SUPPORTED (Image->ImageContext.Machine)) {
    if (!EFI_IMAGE_MACHINE_CROSS_TYPE_SUPPORTED (Image->ImageContext.Machine)) {
      //
      // The PE/COFF loader can support loading image types that can be executed.
      // If we loaded an image type that we can not execute return EFI_UNSUPORTED.
      //
      return EFI_UNSUPPORTED;
    }
  }

  //
  // Set EFI memory type based on ImageType
  //
  switch (Image->ImageContext.ImageType) {
  case EFI_IMAGE_SUBSYSTEM_EFI_APPLICATION:
    Image->ImageContext.ImageCodeMemoryType = EfiLoaderCode;
    Image->ImageContext.ImageDataMemoryType = EfiLoaderData;
    break;
  case EFI_IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER:
    Image->ImageContext.ImageCodeMemoryType = EfiBootServicesCode;
    Image->ImageContext.ImageDataMemoryType = EfiBootServicesData;
    break;
  case EFI_IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER:
  case EFI_IMAGE_SUBSYSTEM_SAL_RUNTIME_DRIVER:
    Image->ImageContext.ImageCodeMemoryType = EfiRuntimeServicesCode;
    Image->ImageContext.ImageDataMemoryType = EfiRuntimeServicesData;
    break;
  default:
    Image->ImageContext.ImageError = IMAGE_ERROR_INVALID_SUBSYSTEM;
    return EFI_UNSUPPORTED;
  }

  //
  // Allocate memory of the correct memory type aligned on the required image boundry
  //
  DstBufAlocated = FALSE;
  if (DstBuffer == 0) {
    //
    // Allocate Destination Buffer as caller did not pass it in
    //

    if (Image->ImageContext.SectionAlignment > EFI_PAGE_SIZE) {
      Size = (UINTN)Image->ImageContext.ImageSize + Image->ImageContext.SectionAlignment;
    } else {
      Size = (UINTN)Image->ImageContext.ImageSize;
    }

    Image->NumberOfPages = EFI_SIZE_TO_PAGES (Size);

    //
    // If the image relocations have not been stripped, then load at any address.
    // Otherwise load at the address at which it was linked.
    //
    // Memory below 1MB should be treated reserved for CSM and there should be
    // no modules whose preferred load addresses are below 1MB.
    //
    Status = EFI_OUT_OF_RESOURCES;
    //
    // If Loading Module At Fixed Address feature is enabled, the module should be loaded to
    // a specified address.
    //
    if (FixedPcdGet64(PcdLoadModuleAtFixAddressEnable) != 0 ) {
      Status = GetPeCoffImageFixLoadingAssignedAddress (&(Image->ImageContext));

      if (EFI_ERROR (Status))  {
          //
      	  // If the code memory is not ready, invoke CoreAllocatePage with AllocateAnyPages to load the driver.
      	  //
          DEBUG ((EFI_D_INFO|EFI_D_LOAD, "LOADING MODULE FIXED ERROR: Loading module at fixed address failed since specified memory is not available.\n"));
        
          Status = CoreAllocatePages (
                     AllocateAnyPages,
                     (EFI_MEMORY_TYPE) (Image->ImageContext.ImageCodeMemoryType),
                     Image->NumberOfPages,
                     &Image->ImageContext.ImageAddress
                     );         
      } 
    } else {
      if (Image->ImageContext.ImageAddress >= 0x100000 || Image->ImageContext.RelocationsStripped) {
        Status = CoreAllocatePages (
                   AllocateAddress,
                   (EFI_MEMORY_TYPE) (Image->ImageContext.ImageCodeMemoryType),
                   Image->NumberOfPages,
                   &Image->ImageContext.ImageAddress
                   );
      }
      if (EFI_ERROR (Status) && !Image->ImageContext.RelocationsStripped) {
        Status = CoreAllocatePages (
                   AllocateAnyPages,
                   (EFI_MEMORY_TYPE) (Image->ImageContext.ImageCodeMemoryType),
                   Image->NumberOfPages,
                   &Image->ImageContext.ImageAddress
                   );
      }
    }
    if (EFI_ERROR (Status)) {
      return Status;
    }
    DstBufAlocated = TRUE;
  } else {
    //
    // Caller provided the destination buffer
    //

    if (Image->ImageContext.RelocationsStripped && (Image->ImageContext.ImageAddress != DstBuffer)) {
      //
      // If the image relocations were stripped, and the caller provided a
      // destination buffer address that does not match the address that the
      // image is linked at, then the image cannot be loaded.
      //
      return EFI_INVALID_PARAMETER;
    }

    if (Image->NumberOfPages != 0 &&
        Image->NumberOfPages <
        (EFI_SIZE_TO_PAGES ((UINTN)Image->ImageContext.ImageSize + Image->ImageContext.SectionAlignment))) {
      Image->NumberOfPages = EFI_SIZE_TO_PAGES ((UINTN)Image->ImageContext.ImageSize + Image->ImageContext.SectionAlignment);
      return EFI_BUFFER_TOO_SMALL;
    }

    Image->NumberOfPages = EFI_SIZE_TO_PAGES ((UINTN)Image->ImageContext.ImageSize + Image->ImageContext.SectionAlignment);
    Image->ImageContext.ImageAddress = DstBuffer;
  }

  Image->ImageBasePage = Image->ImageContext.ImageAddress;
  if (!Image->ImageContext.IsTeImage) {
    Image->ImageContext.ImageAddress =
        (Image->ImageContext.ImageAddress + Image->ImageContext.SectionAlignment - 1) &
        ~((UINTN)Image->ImageContext.SectionAlignment - 1);
  }

  //
  // Load the image from the file into the allocated memory
  //
  Status = PeCoffLoaderLoadImage (&Image->ImageContext);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  //
  // If this is a Runtime Driver, then allocate memory for the FixupData that
  // is used to relocate the image when SetVirtualAddressMap() is called. The
  // relocation is done by the Runtime AP.
  //
  if ((Attribute & EFI_LOAD_PE_IMAGE_ATTRIBUTE_RUNTIME_REGISTRATION) != 0) {
    if (Image->ImageContext.ImageType == EFI_IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER) {
      Image->ImageContext.FixupData = AllocateRuntimePool ((UINTN)(Image->ImageContext.FixupDataSize));
      if (Image->ImageContext.FixupData == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        goto Done;
      }
    }
  }

  //
  // Relocate the image in memory
  //
  Status = PeCoffLoaderRelocateImage (&Image->ImageContext);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  //
  // Flush the Instruction Cache
  //
  InvalidateInstructionCacheRange ((VOID *)(UINTN)Image->ImageContext.ImageAddress, (UINTN)Image->ImageContext.ImageSize);

  //
  // Copy the machine type from the context to the image private data. This
  // is needed during image unload to know if we should call an EBC protocol
  // to unload the image.
  //
  Image->Machine = Image->ImageContext.Machine;

  //
  // Get the image entry point. If it's an EBC image, then call into the
  // interpreter to create a thunk for the entry point and use the returned
  // value for the entry point.
  //
  Image->EntryPoint   = (EFI_IMAGE_ENTRY_POINT)(UINTN)Image->ImageContext.EntryPoint;
  if (Image->ImageContext.Machine == EFI_IMAGE_MACHINE_EBC) {
    //
    // Locate the EBC interpreter protocol
    //
    Status = CoreLocateProtocol (&gEfiEbcProtocolGuid, NULL, (VOID **)&Image->Ebc);
    if (EFI_ERROR(Status)) {
      DEBUG ((DEBUG_LOAD | DEBUG_ERROR, "CoreLoadPeImage: There is no EBC interpreter for an EBC image.\n"));
      goto Done;
    }

    //
    // Register a callback for flushing the instruction cache so that created
    // thunks can be flushed.
    //
    Status = Image->Ebc->RegisterICacheFlush (Image->Ebc, (EBC_ICACHE_FLUSH)InvalidateInstructionCacheRange);
    if (EFI_ERROR(Status)) {
      goto Done;
    }

    //
    // Create a thunk for the image's entry point. This will be the new
    // entry point for the image.
    //
    Status = Image->Ebc->CreateThunk (
                           Image->Ebc,
                           Image->Handle,
                           (VOID *)(UINTN) Image->ImageContext.EntryPoint,
                           (VOID **) &Image->EntryPoint
                           );
    if (EFI_ERROR(Status)) {
      goto Done;
    }
  }

  //
  // Fill in the image information for the Loaded Image Protocol
  //
  Image->Type               = Image->ImageContext.ImageType;
  Image->Info.ImageBase     = (VOID *)(UINTN)Image->ImageContext.ImageAddress;
  Image->Info.ImageSize     = Image->ImageContext.ImageSize;
  Image->Info.ImageCodeType = (EFI_MEMORY_TYPE) (Image->ImageContext.ImageCodeMemoryType);
  Image->Info.ImageDataType = (EFI_MEMORY_TYPE) (Image->ImageContext.ImageDataMemoryType);
  if ((Attribute & EFI_LOAD_PE_IMAGE_ATTRIBUTE_RUNTIME_REGISTRATION) != 0) {
    if (Image->ImageContext.ImageType == EFI_IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER) {
      //
      // Make a list off all the RT images so we can let the RT AP know about them.
      //
      Image->RuntimeData = AllocateRuntimePool (sizeof(EFI_RUNTIME_IMAGE_ENTRY));
      if (Image->RuntimeData == NULL) {
        goto Done;
      }
      Image->RuntimeData->ImageBase      = Image->Info.ImageBase;
      Image->RuntimeData->ImageSize      = (UINT64) (Image->Info.ImageSize);
      Image->RuntimeData->RelocationData = Image->ImageContext.FixupData;
      Image->RuntimeData->Handle         = Image->Handle;
      InsertTailList (&gRuntime->ImageHead, &Image->RuntimeData->Link);
    }
  }

  //
  // Fill in the entry point of the image if it is available
  //
  if (EntryPoint != NULL) {
    *EntryPoint = Image->ImageContext.EntryPoint;
  }

  //
  // Print the load address and the PDB file name if it is available
  //

  DEBUG_CODE_BEGIN ();

    UINTN Index;
    UINTN StartIndex;
    CHAR8 EfiFileName[256];


    DEBUG ((DEBUG_INFO | DEBUG_LOAD,
           "Loading driver at 0x%11p EntryPoint=0x%11p ",
           (VOID *)(UINTN) Image->ImageContext.ImageAddress,
           FUNCTION_ENTRY_POINT (Image->ImageContext.EntryPoint)));


    //
    // Print Module Name by Pdb file path.
    // Windows and Unix style file path are all trimmed correctly.
    //
    if (Image->ImageContext.PdbPointer != NULL) {
      StartIndex = 0;
      for (Index = 0; Image->ImageContext.PdbPointer[Index] != 0; Index++) {
        if ((Image->ImageContext.PdbPointer[Index] == '\\') || (Image->ImageContext.PdbPointer[Index] == '/')) {
          StartIndex = Index + 1;
        }
      }
      //
      // Copy the PDB file name to our temporary string, and replace .pdb with .efi
      // The PDB file name is limited in the range of 0~255.
      // If the length is bigger than 255, trim the redudant characters to avoid overflow in array boundary.
      //
      for (Index = 0; Index < sizeof (EfiFileName) - 4; Index++) {
        EfiFileName[Index] = Image->ImageContext.PdbPointer[Index + StartIndex];
        if (EfiFileName[Index] == 0) {
          EfiFileName[Index] = '.';
        }
        if (EfiFileName[Index] == '.') {
          EfiFileName[Index + 1] = 'e';
          EfiFileName[Index + 2] = 'f';
          EfiFileName[Index + 3] = 'i';
          EfiFileName[Index + 4] = 0;
          break;
        }
      }

      if (Index == sizeof (EfiFileName) - 4) {
        EfiFileName[Index] = 0;
      }
      DEBUG ((DEBUG_INFO | DEBUG_LOAD, "%a", EfiFileName)); // &Image->ImageContext.PdbPointer[StartIndex]));
    }
    DEBUG ((DEBUG_INFO | DEBUG_LOAD, "\n"));

  DEBUG_CODE_END ();

  return EFI_SUCCESS;

Done:

  //
  // Free memory.
  //

  if (DstBufAlocated) {
    CoreFreePages (Image->ImageContext.ImageAddress, Image->NumberOfPages);
  }

  if (Image->ImageContext.FixupData != NULL) {
    CoreFreePool (Image->ImageContext.FixupData);
  }

  return Status;
}



/**
  Get the image's private data from its handle.

  @param  ImageHandle             The image handle

  @return Return the image private data associated with ImageHandle.

**/
LOADED_IMAGE_PRIVATE_DATA *
CoreLoadedImageInfo (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                 Status;
  EFI_LOADED_IMAGE_PROTOCOL  *LoadedImage;
  LOADED_IMAGE_PRIVATE_DATA  *Image;

  Status = CoreHandleProtocol (
             ImageHandle,
             &gEfiLoadedImageProtocolGuid,
             (VOID **)&LoadedImage
             );
  if (!EFI_ERROR (Status)) {
    Image = LOADED_IMAGE_PRIVATE_DATA_FROM_THIS (LoadedImage);
  } else {
    DEBUG ((DEBUG_LOAD, "CoreLoadedImageInfo: Not an ImageHandle %p\n", ImageHandle));
    Image = NULL;
  }

  return Image;
}


/**
  Unloads EFI image from memory.

  @param  Image                   EFI image
  @param  FreePage                Free allocated pages

**/
VOID
CoreUnloadAndCloseImage (
  IN LOADED_IMAGE_PRIVATE_DATA  *Image,
  IN BOOLEAN                    FreePage
  )
{
  EFI_STATUS                          Status;
  UINTN                               HandleCount;
  EFI_HANDLE                          *HandleBuffer;
  UINTN                               HandleIndex;
  EFI_GUID                            **ProtocolGuidArray;
  UINTN                               ArrayCount;
  UINTN                               ProtocolIndex;
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *OpenInfo;
  UINTN                               OpenInfoCount;
  UINTN                               OpenInfoIndex;

  if (Image->Ebc != NULL) {
    //
    // If EBC protocol exists we must perform cleanups for this image.
    //
    Image->Ebc->UnloadImage (Image->Ebc, Image->Handle);
  }

  //
  // Unload image, free Image->ImageContext->ModHandle
  //
  PeCoffLoaderUnloadImage (&Image->ImageContext);

  //
  // Free our references to the image handle
  //
  if (Image->Handle != NULL) {

    Status = CoreLocateHandleBuffer (
               AllHandles,
               NULL,
               NULL,
               &HandleCount,
               &HandleBuffer
               );
    if (!EFI_ERROR (Status)) {
      for (HandleIndex = 0; HandleIndex < HandleCount; HandleIndex++) {
        Status = CoreProtocolsPerHandle (
                   HandleBuffer[HandleIndex],
                   &ProtocolGuidArray,
                   &ArrayCount
                   );
        if (!EFI_ERROR (Status)) {
          for (ProtocolIndex = 0; ProtocolIndex < ArrayCount; ProtocolIndex++) {
            Status = CoreOpenProtocolInformation (
                       HandleBuffer[HandleIndex],
                       ProtocolGuidArray[ProtocolIndex],
                       &OpenInfo,
                       &OpenInfoCount
                       );
            if (!EFI_ERROR (Status)) {
              for (OpenInfoIndex = 0; OpenInfoIndex < OpenInfoCount; OpenInfoIndex++) {
                if (OpenInfo[OpenInfoIndex].AgentHandle == Image->Handle) {
                  Status = CoreCloseProtocol (
                             HandleBuffer[HandleIndex],
                             ProtocolGuidArray[ProtocolIndex],
                             Image->Handle,
                             OpenInfo[OpenInfoIndex].ControllerHandle
                             );
                }
              }
              if (OpenInfo != NULL) {
                CoreFreePool(OpenInfo);
              }
            }
          }
          if (ProtocolGuidArray != NULL) {
            CoreFreePool(ProtocolGuidArray);
          }
        }
      }
      if (HandleBuffer != NULL) {
        CoreFreePool (HandleBuffer);
      }
    }

    CoreRemoveDebugImageInfoEntry (Image->Handle);

    Status = CoreUninstallProtocolInterface (
               Image->Handle,
               &gEfiLoadedImageDevicePathProtocolGuid,
               Image->LoadedImageDevicePath
               );

    Status = CoreUninstallProtocolInterface (
               Image->Handle,
               &gEfiLoadedImageProtocolGuid,
               &Image->Info
               );

    if (Image->ImageContext.HiiResourceData != 0) {
      Status = CoreUninstallProtocolInterface (
                 Image->Handle,
                 &gEfiHiiPackageListProtocolGuid,
                 (VOID *) (UINTN) Image->ImageContext.HiiResourceData
                 );
    }

  }

  if (Image->RuntimeData != NULL) {
    if (Image->RuntimeData->Link.ForwardLink != NULL) {
      //
      // Remove the Image from the Runtime Image list as we are about to Free it!
      //
      RemoveEntryList (&Image->RuntimeData->Link);
    }
    CoreFreePool (Image->RuntimeData);
  }

  //
  // Free the Image from memory
  //
  if ((Image->ImageBasePage != 0) && FreePage) {
    CoreFreePages (Image->ImageBasePage, Image->NumberOfPages);
  }

  //
  // Done with the Image structure
  //
  if (Image->Info.FilePath != NULL) {
    CoreFreePool (Image->Info.FilePath);
  }

  if (Image->LoadedImageDevicePath != NULL) {
    CoreFreePool (Image->LoadedImageDevicePath);
  }

  if (Image->FixupData != NULL) {
    CoreFreePool (Image->FixupData);
  }

  CoreFreePool (Image);
}


/**
  Loads an EFI image into memory and returns a handle to the image.

  @param  BootPolicy              If TRUE, indicates that the request originates
                                  from the boot manager, and that the boot
                                  manager is attempting to load FilePath as a
                                  boot selection.
  @param  ParentImageHandle       The caller's image handle.
  @param  FilePath                The specific file path from which the image is
                                  loaded.
  @param  SourceBuffer            If not NULL, a pointer to the memory location
                                  containing a copy of the image to be loaded.
  @param  SourceSize              The size in bytes of SourceBuffer.
  @param  DstBuffer               The buffer to store the image
  @param  NumberOfPages           If not NULL, it inputs a pointer to the page
                                  number of DstBuffer and outputs a pointer to
                                  the page number of the image. If this number is
                                  not enough,  return EFI_BUFFER_TOO_SMALL and
                                  this parameter contains the required number.
  @param  ImageHandle             Pointer to the returned image handle that is
                                  created when the image is successfully loaded.
  @param  EntryPoint              A pointer to the entry point
  @param  Attribute               The bit mask of attributes to set for the load
                                  PE image

  @retval EFI_SUCCESS             The image was loaded into memory.
  @retval EFI_NOT_FOUND           The FilePath was not found.
  @retval EFI_INVALID_PARAMETER   One of the parameters has an invalid value.
  @retval EFI_BUFFER_TOO_SMALL    The buffer is too small
  @retval EFI_UNSUPPORTED         The image type is not supported, or the device
                                  path cannot be parsed to locate the proper
                                  protocol for loading the file.
  @retval EFI_OUT_OF_RESOURCES    Image was not loaded due to insufficient
                                  resources.

**/
EFI_STATUS
CoreLoadImageCommon (
  IN  BOOLEAN                          BootPolicy,
  IN  EFI_HANDLE                       ParentImageHandle,
  IN  EFI_DEVICE_PATH_PROTOCOL         *FilePath,
  IN  VOID                             *SourceBuffer       OPTIONAL,
  IN  UINTN                            SourceSize,
  IN  EFI_PHYSICAL_ADDRESS             DstBuffer           OPTIONAL,
  IN OUT UINTN                         *NumberOfPages      OPTIONAL,
  OUT EFI_HANDLE                       *ImageHandle,
  OUT EFI_PHYSICAL_ADDRESS             *EntryPoint         OPTIONAL,
  IN  UINT32                           Attribute
  )
{
  LOADED_IMAGE_PRIVATE_DATA  *Image;
  LOADED_IMAGE_PRIVATE_DATA  *ParentImage;
  IMAGE_FILE_HANDLE          FHand;
  EFI_STATUS                 Status;
  EFI_STATUS                 SecurityStatus;
  EFI_HANDLE                 DeviceHandle;
  UINT32                     AuthenticationStatus;
  EFI_DEVICE_PATH_PROTOCOL   *OriginalFilePath;
  EFI_DEVICE_PATH_PROTOCOL   *HandleFilePath;
  UINTN                      FilePathSize;

  SecurityStatus = EFI_SUCCESS;

  ASSERT (gEfiCurrentTpl < TPL_NOTIFY);
  ParentImage = NULL;

  //
  // The caller must pass in a valid ParentImageHandle
  //
  if (ImageHandle == NULL || ParentImageHandle == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ParentImage = CoreLoadedImageInfo (ParentImageHandle);
  if (ParentImage == NULL) {
    DEBUG((DEBUG_LOAD|DEBUG_ERROR, "LoadImageEx: Parent handle not an image handle\n"));
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (&FHand, sizeof (IMAGE_FILE_HANDLE));
  FHand.Signature  = IMAGE_FILE_HANDLE_SIGNATURE;
  OriginalFilePath = FilePath;
  HandleFilePath   = FilePath;
  DeviceHandle     = NULL;
  Status           = EFI_SUCCESS;
  AuthenticationStatus = 0;
  //
  // If the caller passed a copy of the file, then just use it
  //
  if (SourceBuffer != NULL) {
    FHand.Source     = SourceBuffer;
    FHand.SourceSize = SourceSize;
    CoreLocateDevicePath (&gEfiDevicePathProtocolGuid, &HandleFilePath, &DeviceHandle);
    if (SourceSize > 0) {
      Status = EFI_SUCCESS;
    } else {
      Status = EFI_LOAD_ERROR;
    }
  } else {
    if (FilePath == NULL) {
      return EFI_INVALID_PARAMETER;
    }
    //
    // Get the source file buffer by its device path.
    //
    FHand.Source = GetFileBufferByFilePath (
                      BootPolicy, 
                      FilePath,
                      &FHand.SourceSize,
                      &AuthenticationStatus
                      );
    if (FHand.Source == NULL) {
      Status = EFI_NOT_FOUND;
    } else {
      //
      // Try to get the image device handle by checking the match protocol.
      //
      FHand.FreeBuffer = TRUE;
      Status = CoreLocateDevicePath (&gEfiFirmwareVolume2ProtocolGuid, &HandleFilePath, &DeviceHandle);
      if (EFI_ERROR (Status)) {
        HandleFilePath = FilePath;
        Status = CoreLocateDevicePath (&gEfiSimpleFileSystemProtocolGuid, &HandleFilePath, &DeviceHandle);
        if (EFI_ERROR (Status)) {
          if (!BootPolicy) {
            HandleFilePath = FilePath;
            Status = CoreLocateDevicePath (&gEfiLoadFile2ProtocolGuid, &HandleFilePath, &DeviceHandle);
          }
          if (EFI_ERROR (Status)) {
            HandleFilePath = FilePath;
            Status = CoreLocateDevicePath (&gEfiLoadFileProtocolGuid, &HandleFilePath, &DeviceHandle);
          }
        }
      }
    }
  }

  if (Status == EFI_ALREADY_STARTED) {
    Image = NULL;
    goto Done;
  } else if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Verify the Authentication Status through the Security Architectural Protocol
  //
  if ((gSecurity != NULL) && (OriginalFilePath != NULL)) {
    SecurityStatus = gSecurity->FileAuthenticationState (
                                  gSecurity,
                                  AuthenticationStatus,
                                  OriginalFilePath
                                  );
    if (EFI_ERROR (SecurityStatus) && SecurityStatus != EFI_SECURITY_VIOLATION) {
      Status = SecurityStatus;
      Image = NULL;
      goto Done;
    }
  }


  //
  // Allocate a new image structure
  //
  Image = AllocateZeroPool (sizeof(LOADED_IMAGE_PRIVATE_DATA));
  if (Image == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Pull out just the file portion of the DevicePath for the LoadedImage FilePath
  //
  FilePath = OriginalFilePath;
  Status = CoreHandleProtocol (DeviceHandle, &gEfiDevicePathProtocolGuid, (VOID **)&HandleFilePath);
  if (!EFI_ERROR (Status)) {
    FilePathSize = GetDevicePathSize (HandleFilePath) - sizeof(EFI_DEVICE_PATH_PROTOCOL);
    FilePath = (EFI_DEVICE_PATH_PROTOCOL *) (((UINT8 *)FilePath) + FilePathSize );
  }

  //
  // Initialize the fields for an internal driver
  //
  Image->Signature         = LOADED_IMAGE_PRIVATE_DATA_SIGNATURE;
  Image->Info.SystemTable  = gDxeCoreST;
  Image->Info.DeviceHandle = DeviceHandle;
  Image->Info.Revision     = EFI_LOADED_IMAGE_PROTOCOL_REVISION;
  Image->Info.FilePath     = DuplicateDevicePath (FilePath);
  Image->Info.ParentHandle = ParentImageHandle;


  if (NumberOfPages != NULL) {
    Image->NumberOfPages = *NumberOfPages ;
  } else {
    Image->NumberOfPages = 0 ;
  }

  //
  // Install the protocol interfaces for this image
  // don't fire notifications yet
  //
  Status = CoreInstallProtocolInterfaceNotify (
             &Image->Handle,
             &gEfiLoadedImageProtocolGuid,
             EFI_NATIVE_INTERFACE,
             &Image->Info,
             FALSE
             );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  //
  // Load the image.  If EntryPoint is Null, it will not be set.
  //
  Status = CoreLoadPeImage (BootPolicy, &FHand, Image, DstBuffer, EntryPoint, Attribute);
  if (EFI_ERROR (Status)) {
    if ((Status == EFI_BUFFER_TOO_SMALL) || (Status == EFI_OUT_OF_RESOURCES)) {
      if (NumberOfPages != NULL) {
        *NumberOfPages = Image->NumberOfPages;
      }
    }
    goto Done;
  }

  if (NumberOfPages != NULL) {
    *NumberOfPages = Image->NumberOfPages;
  }

  //
  // Register the image in the Debug Image Info Table if the attribute is set
  //
  if ((Attribute & EFI_LOAD_PE_IMAGE_ATTRIBUTE_DEBUG_IMAGE_INFO_TABLE_REGISTRATION) != 0) {
    CoreNewDebugImageInfoEntry (EFI_DEBUG_IMAGE_INFO_TYPE_NORMAL, &Image->Info, Image->Handle);
  }

  //
  //Reinstall loaded image protocol to fire any notifications
  //
  Status = CoreReinstallProtocolInterface (
             Image->Handle,
             &gEfiLoadedImageProtocolGuid,
             &Image->Info,
             &Image->Info
             );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  //
  // If DevicePath parameter to the LoadImage() is not NULL, then make a copy of DevicePath,
  // otherwise Loaded Image Device Path Protocol is installed with a NULL interface pointer.
  //
  if (OriginalFilePath != NULL) {
    Image->LoadedImageDevicePath = DuplicateDevicePath (OriginalFilePath);
  }

  //
  // Install Loaded Image Device Path Protocol onto the image handle of a PE/COFE image
  //
  Status = CoreInstallProtocolInterface (
            &Image->Handle,
            &gEfiLoadedImageDevicePathProtocolGuid,
            EFI_NATIVE_INTERFACE,
            Image->LoadedImageDevicePath
            );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  //
  // Install HII Package List Protocol onto the image handle
  //
  if (Image->ImageContext.HiiResourceData != 0) {
    Status = CoreInstallProtocolInterface (
               &Image->Handle,
               &gEfiHiiPackageListProtocolGuid,
               EFI_NATIVE_INTERFACE,
               (VOID *) (UINTN) Image->ImageContext.HiiResourceData
               );
    if (EFI_ERROR (Status)) {
      goto Done;
    }
  }

  //
  // Success.  Return the image handle
  //
  *ImageHandle = Image->Handle;

Done:
  //
  // All done accessing the source file
  // If we allocated the Source buffer, free it
  //
  if (FHand.FreeBuffer) {
    CoreFreePool (FHand.Source);
  }

  //
  // There was an error.  If there's an Image structure, free it
  //
  if (EFI_ERROR (Status)) {
    if (Image != NULL) {
      CoreUnloadAndCloseImage (Image, (BOOLEAN)(DstBuffer == 0));
      *ImageHandle = NULL;
    }
  } else if (EFI_ERROR (SecurityStatus)) {
    Status = SecurityStatus;
  }

  return Status;
}




/**
  Loads an EFI image into memory and returns a handle to the image.

  @param  BootPolicy              If TRUE, indicates that the request originates
                                  from the boot manager, and that the boot
                                  manager is attempting to load FilePath as a
                                  boot selection.
  @param  ParentImageHandle       The caller's image handle.
  @param  FilePath                The specific file path from which the image is
                                  loaded.
  @param  SourceBuffer            If not NULL, a pointer to the memory location
                                  containing a copy of the image to be loaded.
  @param  SourceSize              The size in bytes of SourceBuffer.
  @param  ImageHandle             Pointer to the returned image handle that is
                                  created when the image is successfully loaded.

  @retval EFI_SUCCESS             The image was loaded into memory.
  @retval EFI_NOT_FOUND           The FilePath was not found.
  @retval EFI_INVALID_PARAMETER   One of the parameters has an invalid value.
  @retval EFI_UNSUPPORTED         The image type is not supported, or the device
                                  path cannot be parsed to locate the proper
                                  protocol for loading the file.
  @retval EFI_OUT_OF_RESOURCES    Image was not loaded due to insufficient
                                  resources.

**/
EFI_STATUS
EFIAPI
CoreLoadImage (
  IN BOOLEAN                    BootPolicy,
  IN EFI_HANDLE                 ParentImageHandle,
  IN EFI_DEVICE_PATH_PROTOCOL   *FilePath,
  IN VOID                       *SourceBuffer   OPTIONAL,
  IN UINTN                      SourceSize,
  OUT EFI_HANDLE                *ImageHandle
  )
{
  EFI_STATUS    Status;
  UINT64        Tick;

  Tick = 0;
  PERF_CODE (
    Tick = GetPerformanceCounter ();
  );

  Status = CoreLoadImageCommon (
             BootPolicy,
             ParentImageHandle,
             FilePath,
             SourceBuffer,
             SourceSize,
             (EFI_PHYSICAL_ADDRESS) (UINTN) NULL,
             NULL,
             ImageHandle,
             NULL,
             EFI_LOAD_PE_IMAGE_ATTRIBUTE_RUNTIME_REGISTRATION | EFI_LOAD_PE_IMAGE_ATTRIBUTE_DEBUG_IMAGE_INFO_TABLE_REGISTRATION
             );

  PERF_START (*ImageHandle, "LoadImage:", NULL, Tick);
  PERF_END (*ImageHandle, "LoadImage:", NULL, 0);

  return Status;
}



/**
  Loads an EFI image into memory and returns a handle to the image with extended parameters.

  @param  This                    Calling context
  @param  ParentImageHandle       The caller's image handle.
  @param  FilePath                The specific file path from which the image is
                                  loaded.
  @param  SourceBuffer            If not NULL, a pointer to the memory location
                                  containing a copy of the image to be loaded.
  @param  SourceSize              The size in bytes of SourceBuffer.
  @param  DstBuffer               The buffer to store the image.
  @param  NumberOfPages           For input, specifies the space size of the
                                  image by caller if not NULL. For output,
                                  specifies the actual space size needed.
  @param  ImageHandle             Image handle for output.
  @param  EntryPoint              Image entry point for output.
  @param  Attribute               The bit mask of attributes to set for the load
                                  PE image.

  @retval EFI_SUCCESS             The image was loaded into memory.
  @retval EFI_NOT_FOUND           The FilePath was not found.
  @retval EFI_INVALID_PARAMETER   One of the parameters has an invalid value.
  @retval EFI_UNSUPPORTED         The image type is not supported, or the device
                                  path cannot be parsed to locate the proper
                                  protocol for loading the file.
  @retval EFI_OUT_OF_RESOURCES    Image was not loaded due to insufficient
                                  resources.

**/
EFI_STATUS
EFIAPI
CoreLoadImageEx (
  IN  EFI_PE32_IMAGE_PROTOCOL          *This,
  IN  EFI_HANDLE                       ParentImageHandle,
  IN  EFI_DEVICE_PATH_PROTOCOL         *FilePath,
  IN  VOID                             *SourceBuffer       OPTIONAL,
  IN  UINTN                            SourceSize,
  IN  EFI_PHYSICAL_ADDRESS             DstBuffer           OPTIONAL,
  OUT UINTN                            *NumberOfPages      OPTIONAL,
  OUT EFI_HANDLE                       *ImageHandle,
  OUT EFI_PHYSICAL_ADDRESS             *EntryPoint         OPTIONAL,
  IN  UINT32                           Attribute
  )
{
  return CoreLoadImageCommon (
           TRUE,
           ParentImageHandle,
           FilePath,
           SourceBuffer,
           SourceSize,
           DstBuffer,
           NumberOfPages,
           ImageHandle,
           EntryPoint,
           Attribute
           );
}


/**
  Transfer control to a loaded image's entry point.

  @param  ImageHandle             Handle of image to be started.
  @param  ExitDataSize            Pointer of the size to ExitData
  @param  ExitData                Pointer to a pointer to a data buffer that
                                  includes a Null-terminated Unicode string,
                                  optionally followed by additional binary data.
                                  The string is a description that the caller may
                                  use to further indicate the reason for the
                                  image's exit.

  @retval EFI_INVALID_PARAMETER   Invalid parameter
  @retval EFI_OUT_OF_RESOURCES    No enough buffer to allocate
  @retval EFI_SUCCESS             Successfully transfer control to the image's
                                  entry point.

**/
EFI_STATUS
EFIAPI
CoreStartImage (
  IN EFI_HANDLE  ImageHandle,
  OUT UINTN      *ExitDataSize,
  OUT CHAR16     **ExitData  OPTIONAL
  )
{
  EFI_STATUS                    Status;
  LOADED_IMAGE_PRIVATE_DATA     *Image;
  LOADED_IMAGE_PRIVATE_DATA     *LastImage;
  UINT64                        HandleDatabaseKey;
  UINTN                         SetJumpFlag;

  Image = CoreLoadedImageInfo (ImageHandle);
  if (Image == NULL  ||  Image->Started) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // The image to be started must have the machine type supported by DxeCore.
  //
  ASSERT (EFI_IMAGE_MACHINE_TYPE_SUPPORTED (Image->Machine));
  if (!EFI_IMAGE_MACHINE_TYPE_SUPPORTED (Image->Machine)) {
    return EFI_UNSUPPORTED;
  }

  //
  // Don't profile Objects or invalid start requests
  //
  PERF_START (ImageHandle, "StartImage:", NULL, 0);


  //
  // Push the current start image context, and
  // link the current image to the head.   This is the
  // only image that can call Exit()
  //
  HandleDatabaseKey = CoreGetHandleDatabaseKey ();
  LastImage         = mCurrentImage;
  mCurrentImage     = Image;
  Image->Tpl        = gEfiCurrentTpl;

  //
  // Set long jump for Exit() support
  // JumpContext must be aligned on a CPU specific boundary.
  // Overallocate the buffer and force the required alignment
  //
  Image->JumpBuffer = AllocatePool (sizeof (BASE_LIBRARY_JUMP_BUFFER) + BASE_LIBRARY_JUMP_BUFFER_ALIGNMENT);
  if (Image->JumpBuffer == NULL) {
    PERF_END (ImageHandle, "StartImage:", NULL, 0);
    return EFI_OUT_OF_RESOURCES;
  }
  Image->JumpContext = ALIGN_POINTER (Image->JumpBuffer, BASE_LIBRARY_JUMP_BUFFER_ALIGNMENT);

  SetJumpFlag = SetJump (Image->JumpContext);
  //
  // The initial call to SetJump() must always return 0.
  // Subsequent calls to LongJump() cause a non-zero value to be returned by SetJump().
  //
  if (SetJumpFlag == 0) {
    //
    // Call the image's entry point
    //
    Image->Started = TRUE;
    Image->Status = Image->EntryPoint (ImageHandle, Image->Info.SystemTable);

    //
    // Add some debug information if the image returned with error.
    // This make the user aware and check if the driver image have already released
    // all the resource in this situation.
    //
    DEBUG_CODE_BEGIN ();
      if (EFI_ERROR (Image->Status)) {
        DEBUG ((DEBUG_ERROR, "Error: Image at %11p start failed: %r\n", Image->Info.ImageBase, Image->Status));
      }
    DEBUG_CODE_END ();

    //
    // If the image returns, exit it through Exit()
    //
    CoreExit (ImageHandle, Image->Status, 0, NULL);
  }

  //
  // Image has completed.  Verify the tpl is the same
  //
  ASSERT (Image->Tpl == gEfiCurrentTpl);
  CoreRestoreTpl (Image->Tpl);

  CoreFreePool (Image->JumpBuffer);

  //
  // Pop the current start image context
  //
  mCurrentImage = LastImage;

  //
  // Go connect any handles that were created or modified while the image executed.
  //
  CoreConnectHandlesByKey (HandleDatabaseKey);

  //
  // Handle the image's returned ExitData
  //
  DEBUG_CODE_BEGIN ();
    if (Image->ExitDataSize != 0 || Image->ExitData != NULL) {

      DEBUG ((DEBUG_LOAD, "StartImage: ExitDataSize %d, ExitData %p", (UINT32)Image->ExitDataSize, Image->ExitData));
      if (Image->ExitData != NULL) {
        DEBUG ((DEBUG_LOAD, " (%hs)", Image->ExitData));
      }
      DEBUG ((DEBUG_LOAD, "\n"));
    }
  DEBUG_CODE_END ();

  //
  //  Return the exit data to the caller
  //
  if (ExitData != NULL && ExitDataSize != NULL) {
    *ExitDataSize = Image->ExitDataSize;
    *ExitData     = Image->ExitData;
  } else {
    //
    // Caller doesn't want the exit data, free it
    //
    CoreFreePool (Image->ExitData);
    Image->ExitData = NULL;
  }

  //
  // Save the Status because Image will get destroyed if it is unloaded.
  //
  Status = Image->Status;

  //
  // If the image returned an error, or if the image is an application
  // unload it
  //
  if (EFI_ERROR (Image->Status) || Image->Type == EFI_IMAGE_SUBSYSTEM_EFI_APPLICATION) {
    CoreUnloadAndCloseImage (Image, TRUE);
  }

  //
  // Done
  //
  PERF_END (ImageHandle, "StartImage:", NULL, 0);
  return Status;
}

/**
  Terminates the currently loaded EFI image and returns control to boot services.

  @param  ImageHandle             Handle that identifies the image. This
                                  parameter is passed to the image on entry.
  @param  Status                  The image's exit code.
  @param  ExitDataSize            The size, in bytes, of ExitData. Ignored if
                                  ExitStatus is EFI_SUCCESS.
  @param  ExitData                Pointer to a data buffer that includes a
                                  Null-terminated Unicode string, optionally
                                  followed by additional binary data. The string
                                  is a description that the caller may use to
                                  further indicate the reason for the image's
                                  exit.

  @retval EFI_INVALID_PARAMETER   Image handle is NULL or it is not current
                                  image.
  @retval EFI_SUCCESS             Successfully terminates the currently loaded
                                  EFI image.
  @retval EFI_ACCESS_DENIED       Should never reach there.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate pool

**/
EFI_STATUS
EFIAPI
CoreExit (
  IN EFI_HANDLE  ImageHandle,
  IN EFI_STATUS  Status,
  IN UINTN       ExitDataSize,
  IN CHAR16      *ExitData  OPTIONAL
  )
{
  LOADED_IMAGE_PRIVATE_DATA  *Image;
  EFI_TPL                    OldTpl;

  //
  // Prevent possible reentrance to this function
  // for the same ImageHandle
  //
  OldTpl = CoreRaiseTpl (TPL_NOTIFY);

  Image = CoreLoadedImageInfo (ImageHandle);
  if (Image == NULL) {
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }

  if (!Image->Started) {
    //
    // The image has not been started so just free its resources
    //
    CoreUnloadAndCloseImage (Image, TRUE);
    Status = EFI_SUCCESS;
    goto Done;
  }

  //
  // Image has been started, verify this image can exit
  //
  if (Image != mCurrentImage) {
    DEBUG ((DEBUG_LOAD|DEBUG_ERROR, "Exit: Image is not exitable image\n"));
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }

  //
  // Set status
  //
  Image->Status = Status;

  //
  // If there's ExitData info, move it
  //
  if (ExitData != NULL) {
    Image->ExitDataSize = ExitDataSize;
    Image->ExitData = AllocatePool (Image->ExitDataSize);
    if (Image->ExitData == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto Done;
    }
    CopyMem (Image->ExitData, ExitData, Image->ExitDataSize);
  }

  CoreRestoreTpl (OldTpl);
  //
  // return to StartImage
  //
  LongJump (Image->JumpContext, (UINTN)-1);

  //
  // If we return from LongJump, then it is an error
  //
  ASSERT (FALSE);
  Status = EFI_ACCESS_DENIED;
Done:
  CoreRestoreTpl (OldTpl);
  return Status;
}




/**
  Unloads an image.

  @param  ImageHandle             Handle that identifies the image to be
                                  unloaded.

  @retval EFI_SUCCESS             The image has been unloaded.
  @retval EFI_UNSUPPORTED         The image has been sarted, and does not support
                                  unload.
  @retval EFI_INVALID_PARAMPETER  ImageHandle is not a valid image handle.

**/
EFI_STATUS
EFIAPI
CoreUnloadImage (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                 Status;
  LOADED_IMAGE_PRIVATE_DATA  *Image;

  Image = CoreLoadedImageInfo (ImageHandle);
  if (Image == NULL ) {
    //
    // The image handle is not valid
    //
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }

  if (Image->Started) {
    //
    // The image has been started, request it to unload.
    //
    Status = EFI_UNSUPPORTED;
    if (Image->Info.Unload != NULL) {
      Status = Image->Info.Unload (ImageHandle);
    }

  } else {
    //
    // This Image hasn't been started, thus it can be unloaded
    //
    Status = EFI_SUCCESS;
  }


  if (!EFI_ERROR (Status)) {
    //
    // if the Image was not started or Unloaded O.K. then clean up
    //
    CoreUnloadAndCloseImage (Image, TRUE);
  }

Done:
  return Status;
}



/**
  Unload the specified image.

  @param  This                    Indicates the calling context.
  @param  ImageHandle             The specified image handle.

  @retval EFI_INVALID_PARAMETER   Image handle is NULL.
  @retval EFI_UNSUPPORTED         Attempt to unload an unsupported image.
  @retval EFI_SUCCESS             Image successfully unloaded.

**/
EFI_STATUS
EFIAPI
CoreUnloadImageEx (
  IN EFI_PE32_IMAGE_PROTOCOL  *This,
  IN EFI_HANDLE                         ImageHandle
  )
{
  return CoreUnloadImage (ImageHandle);
}
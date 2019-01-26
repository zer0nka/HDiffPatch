// dir_patch.c
// hdiffz dir patch
//
/*
 The MIT License (MIT)
 Copyright (c) 2018-2019 HouSisong
 
 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:
 
 The above copyright notice and this permission notice shall be
 included in all copies of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.
 */
#include "dir_patch.h"
#if (_IS_NEED_DIR_DIFF_PATCH)
#include "dir_patch_private.h"
#include <stdio.h>
#include <string.h>
#include "../../libHDiffPatch/HPatch/patch.h"
#include "../../libHDiffPatch/HPatch/patch_private.h"
#include "../../file_for_patch.h"

static const char* kVersionType="HDIFF19";

#define TUInt hpatch_StreamPos_t

#define  check(value) { if (!(value)){ fprintf(stderr,"check "#value" error!\n");  \
                                       result=hpatch_FALSE; goto clear; } }

#define unpackUIntTo(puint,sclip) \
    check(_TStreamCacheClip_unpackUIntWithTag(sclip,puint,0))


#define unpackToSize(psize,sclip) { \
    TUInt v; check(_TStreamCacheClip_unpackUIntWithTag(sclip,&v,0)); \
    if (sizeof(TUInt)!=sizeof(size_t)) check(v==(size_t)v); \
    *(psize)=(size_t)v; }


static hpatch_BOOL _TDirPatcher_copyFile(const char* oldFileName_utf8,const char* newFileName_utf8,
                                         hpatch_ICopyDataListener* copyListener){
#define _tempCacheSize (hpatch_kStreamCacheSize*4)
    hpatch_BOOL result=hpatch_TRUE;
    TByte        temp_cache[_tempCacheSize];
    hpatch_StreamPos_t pos=0;
    hpatch_TFileStreamInput   oldFile;
    hpatch_TFileStreamOutput  newFile;
    hpatch_TFileStreamInput_init(&oldFile);
    hpatch_TFileStreamOutput_init(&newFile);
    
    check(hpatch_TFileStreamInput_open(&oldFile,oldFileName_utf8));
    if (newFileName_utf8)
        check(hpatch_TFileStreamOutput_open(&newFile,newFileName_utf8,oldFile.base.streamSize));
    while (pos<oldFile.base.streamSize) {
        size_t copyLen=_tempCacheSize;
        if (pos+copyLen>oldFile.base.streamSize)
            copyLen=(size_t)(oldFile.base.streamSize-pos);
        check(oldFile.base.read(&oldFile.base,pos,temp_cache,temp_cache+copyLen));
        if (newFileName_utf8)
            check(newFile.base.write(&newFile.base,pos,temp_cache,temp_cache+copyLen));
        if (copyListener)
            copyListener->copyedData(copyListener,temp_cache,temp_cache+copyLen);
        pos+=copyLen;
    }
    check(!oldFile.fileError);
    check(!newFile.fileError);
    if (newFileName_utf8)
        check(newFile.out_length==newFile.base.streamSize);
clear:
    hpatch_TFileStreamOutput_close(&newFile);
    hpatch_TFileStreamInput_close(&oldFile);
    return result;
#undef _tempCacheSize
}

hpatch_BOOL TDirPatcher_copyFile(const char* oldFileName_utf8,const char* newFileName_utf8,
                                 hpatch_ICopyDataListener* copyListener){
    hpatch_BOOL result=hpatch_TRUE;
    check(newFileName_utf8!=0);
    result=_TDirPatcher_copyFile(oldFileName_utf8,newFileName_utf8,copyListener);
clear:
    return result;
}
hpatch_BOOL TDirPatcher_readFile(const char* oldFileName_utf8,hpatch_ICopyDataListener* copyListener){
    return _TDirPatcher_copyFile(oldFileName_utf8,0,copyListener);
}

char* pushDirPath(char* out_path,char* out_pathEnd,const char* rootDir){
    char*          result=0; //false
    size_t rootDirLen=strlen(rootDir);
    hpatch_BOOL isNeedDirSeparator=(rootDirLen>0)&&(rootDir[rootDirLen-1]!=kPatch_dirSeparator);
    check((rootDirLen+isNeedDirSeparator+1)<=(size_t)(out_pathEnd-out_path));
    memcpy(out_path,rootDir,rootDirLen);
    out_path+=rootDirLen;
    if (isNeedDirSeparator) *out_path++=kPatch_dirSeparator;
    *out_path='\0'; //C string end
    result=out_path;
clear:
    return result;
}
static hpatch_BOOL addingPath(char* out_pathBegin,char* out_pathBufEnd,const char* utf8fileName){
    hpatch_BOOL          result=hpatch_TRUE;
    size_t utf8fileNameSize=strlen(utf8fileName);
    check(utf8fileNameSize+1<=(size_t)(out_pathBufEnd-out_pathBegin));
    memcpy(out_pathBegin,utf8fileName,utf8fileNameSize+1);
clear:
    return result;
}

static void formatDirTagForLoad(char* utf8_path,char* utf8_pathEnd){
    if (kPatch_dirSeparator==kPatch_dirSeparator_saved) return;
    for (;utf8_path<utf8_pathEnd;++utf8_path){
        if ((*utf8_path)!=kPatch_dirSeparator_saved)
            continue;
        else
            *utf8_path=kPatch_dirSeparator;
    }
}

hpatch_BOOL getDirDiffInfoByFile(const char* diffFileName,TDirDiffInfo* out_info){
    hpatch_BOOL          result=hpatch_TRUE;
    hpatch_TFileStreamInput     diffData;
    hpatch_TFileStreamInput_init(&diffData);
    
    check(hpatch_TFileStreamInput_open(&diffData,diffFileName));
    result=getDirDiffInfo(&diffData.base,out_info);
    check(hpatch_TFileStreamInput_close(&diffData));
clear:
    return result;
}

hpatch_BOOL read_dirdiff_head(TDirDiffInfo* out_info,_TDirDiffHead* out_head,
                              const hpatch_TStreamInput* dirDiffFile){
    hpatch_BOOL result=hpatch_TRUE;
    TStreamCacheClip  _headClip;
    TStreamCacheClip* headClip=&_headClip;
    TByte             temp_cache[hpatch_kStreamCacheSize];
    char savedCompressType[hpatch_kMaxPluginTypeLength+1];
    check(out_info!=0);
    out_info->isDirDiff=hpatch_FALSE;
    
    _TStreamCacheClip_init(headClip,dirDiffFile,0,dirDiffFile->streamSize,
                           temp_cache,hpatch_kStreamCacheSize);
    {//type
        char* tempType=out_info->hdiffInfo.compressType;
        if (!_TStreamCacheClip_readType_end(headClip,'&',tempType)) return result;
        if (0!=strcmp(tempType,kVersionType)) return result;
        out_info->isDirDiff=hpatch_TRUE;
    }
    //read compressType
    check(_TStreamCacheClip_readType_end(headClip,'&',savedCompressType));
    //read checksumType
    check(_TStreamCacheClip_readType_end(headClip,'\0',out_info->checksumType));
    out_head->typesEndPos=_TStreamCacheClip_readPosOfSrcStream(headClip);
    {
        TUInt savedValue;
        unpackUIntTo(&savedValue,headClip);  check(savedValue<=1);
        out_info->oldPathIsDir=(hpatch_BOOL)savedValue;
        unpackUIntTo(&savedValue,headClip);  check(savedValue<=1);
        out_info->newPathIsDir=(hpatch_BOOL)savedValue;
        
        unpackToSize(&out_head->oldPathCount,headClip);
        unpackToSize(&out_head->newPathCount,headClip);
        unpackToSize(&out_head->oldPathSumSize,headClip);
        unpackToSize(&out_head->newPathSumSize,headClip);
        unpackToSize(&out_head->oldRefFileCount,headClip);
        unpackToSize(&out_head->newRefFileCount,headClip);
        unpackToSize(&out_head->sameFilePairCount,headClip);
        unpackUIntTo(&out_head->sameFileSize,headClip);
        unpackUIntTo(&out_head->privateExternDataSize,headClip);
        unpackUIntTo(&out_info->externDataSize,headClip);
        unpackToSize(&out_info->checksumByteSize,headClip);
        out_info->checksumOffset=_TStreamCacheClip_readPosOfSrcStream(headClip);
        if (out_info->checksumByteSize)
            check(_TStreamCacheClip_skipData(headClip,out_info->checksumByteSize*4));
        out_head->compressSizeBeginPos=_TStreamCacheClip_readPosOfSrcStream(headClip);
        unpackUIntTo(&out_head->headDataSize,headClip);
        unpackUIntTo(&out_head->headDataCompressedSize,headClip);
        out_info->dirDataIsCompressed=(out_head->headDataCompressedSize>0);
    }
    {
        TStreamInputClip hdiffStream;
        TUInt curPos=_TStreamCacheClip_readPosOfSrcStream(headClip);
        out_head->headDataOffset=curPos;
        curPos+=(out_head->headDataCompressedSize>0)?out_head->headDataCompressedSize:out_head->headDataSize;
        out_head->privateExternDataOffset=curPos;
        curPos+=out_head->privateExternDataSize;
        out_info->externDataOffset=curPos;
        curPos+=out_info->externDataSize;
        out_head->hdiffDataOffset=curPos;
        out_head->hdiffDataSize=dirDiffFile->streamSize-curPos;
        TStreamInputClip_init(&hdiffStream,dirDiffFile,out_head->hdiffDataOffset,
                              out_head->hdiffDataOffset+out_head->hdiffDataSize);
        check(getCompressedDiffInfo(&out_info->hdiffInfo,&hdiffStream.base));
        check(0==strcmp(savedCompressType,out_info->hdiffInfo.compressType));
    }
clear:
    return result;
}

hpatch_BOOL getDirDiffInfo(const hpatch_TStreamInput* diffFile,TDirDiffInfo* out_info){
    _TDirDiffHead head;
    return read_dirdiff_head(out_info,&head,diffFile);
}


hpatch_BOOL TDirPatcher_open(TDirPatcher* self,const hpatch_TStreamInput* dirDiffData,
                             const TDirDiffInfo** out_dirDiffInfo){
    hpatch_BOOL result;
    assert(self->_dirDiffData==0);
    self->_dirDiffData=dirDiffData;
    result=read_dirdiff_head(&self->dirDiffInfo,&self->dirDiffHead,self->_dirDiffData);
    if (result)
        *out_dirDiffInfo=&self->dirDiffInfo;
    return result;
}

static hpatch_BOOL _TStreamCacheClip_readDataTo(TStreamCacheClip* sclip,TByte* out_buf,TByte* bufEnd){
    const size_t maxReadLen=sclip->cacheEnd;
    while (out_buf<bufEnd) {
        const TByte* pdata;
        size_t readLen=bufEnd-out_buf;
        if (readLen>maxReadLen) readLen=maxReadLen;
        pdata=_TStreamCacheClip_readData(sclip,readLen);
        if (pdata==0) return hpatch_FALSE;
        memcpy(out_buf,pdata,readLen);
        out_buf+=readLen;
    }
    return out_buf==bufEnd;
}

static hpatch_BOOL clipCStrsTo(const char* cstrs,const char* cstrsEnd,
                               const char** out_cstrList,size_t cstrCount){
    if (cstrs<cstrsEnd){
        if (cstrsEnd[-1]!='\0') return hpatch_FALSE;
        while ((cstrs<cstrsEnd)&(cstrCount>0)) {
            *out_cstrList=cstrs;  ++out_cstrList; --cstrCount;
            cstrs+=strlen(cstrs)+1; //safe
        }
    }
    return (cstrs==cstrsEnd)&(cstrCount==0);
}

static hpatch_BOOL readIncListTo(TStreamCacheClip* sclip,size_t* out_list,size_t count,size_t check_endValue){
    //endValue: maxValue+1
    hpatch_BOOL result=hpatch_TRUE;
    TUInt backValue=~(TUInt)0;
    size_t i;
    for (i=0; i<count; ++i) {
        TUInt incValue;
        check(_TStreamCacheClip_unpackUIntWithTag(sclip,&incValue,0));
        backValue+=1+incValue;
        check(backValue<check_endValue);
        out_list[i]=(size_t)backValue;
    }
clear:
    return result;
}

static hpatch_BOOL readListTo(TStreamCacheClip* sclip,hpatch_StreamPos_t* out_list,size_t count){
    hpatch_BOOL result=hpatch_TRUE;
    size_t i;
    for (i=0; i<count; ++i) {
        check(_TStreamCacheClip_unpackUIntWithTag(sclip,&out_list[i],0));
    }
clear:
    return result;
}


static hpatch_BOOL readSamePairListTo(TStreamCacheClip* sclip,hpatch_TSameFilePair* out_pairList,size_t pairCount,
                                      size_t check_endNewValue,size_t check_endOldValue){
    //endValue: maxValue+1
    hpatch_BOOL result=hpatch_TRUE;
    TUInt backNewValue=~(TUInt)0;
    TUInt backOldValue=~(TUInt)0;
    size_t i;
    for (i=0; i<pairCount;++i) {
        const TByte*  psign;
        TUInt incOldValue;
        TUInt incNewValue;
        check(_TStreamCacheClip_unpackUIntWithTag(sclip,&incNewValue,0));
        backNewValue+=1+incNewValue;
        check(backNewValue<check_endNewValue);
        out_pairList[i].newIndex=(size_t)backNewValue;
        
        psign=_TStreamCacheClip_accessData(sclip,1);
        check(psign!=0);
        check(_TStreamCacheClip_unpackUIntWithTag(sclip,&incOldValue,1));
        if (0==((*psign)>>(8-1)))
            backOldValue+=1+incOldValue;
        else
            backOldValue=backOldValue+1-incOldValue;
        check(backOldValue<check_endOldValue);
        out_pairList[i].oldIndex=(size_t)backOldValue;
    }
clear:
    return result;
}

static hpatch_BOOL _checksum_append(hpatch_TChecksum* checksumPlugin,hpatch_checksumHandle csHandle,
                                    const hpatch_TStreamInput* data,
                                    hpatch_StreamPos_t begin,hpatch_StreamPos_t end){
    hpatch_BOOL result=hpatch_TRUE;
#define __bufCacheSize (hpatch_kStreamCacheSize*4)
    TByte buf[__bufCacheSize];
    while (begin<end) {
        size_t len=__bufCacheSize;
        if (len>(hpatch_StreamPos_t)(end-begin))
            len=(size_t)(end-begin);
        check(data->read(data,begin,buf,buf+len));
        checksumPlugin->append(csHandle,buf,buf+len);
        begin+=len;
    }
    return hpatch_TRUE;
clear:
    return result;
#undef __bufCacheSize
}

static hpatch_BOOL _do_checksum(TDirPatcher* self,const TByte* checksumTest,TByte* checksumTemp,
                                const hpatch_TStreamInput* data,
                                hpatch_StreamPos_t skipBegin,hpatch_StreamPos_t skipEnd){
    hpatch_BOOL result=hpatch_TRUE;
    size_t checksumByteSize=self->dirDiffInfo.checksumByteSize;
    hpatch_TChecksum*   checksumPlugin=self->_checksumSet.checksumPlugin;
    hpatch_checksumHandle  csHandle=checksumPlugin->open(checksumPlugin);
    checksumPlugin->begin(csHandle);
    if (0<skipBegin){
        check(_checksum_append(checksumPlugin,csHandle,data,0,skipBegin));
    }
    if (skipEnd<data->streamSize){
        check(_checksum_append(checksumPlugin,csHandle,data,skipEnd,data->streamSize));
    }
    checksumPlugin->end(csHandle,checksumTemp,checksumTemp+checksumByteSize);
    check(0==memcmp(checksumTest,checksumTemp,checksumByteSize));
clear:
    if (csHandle) checksumPlugin->close(checksumPlugin,csHandle);
    return result;
}

hpatch_BOOL TDirPatcher_checksum(TDirPatcher* self,const TDirPatchChecksumSet* checksumSet){
    hpatch_BOOL result=hpatch_TRUE;
    hpatch_BOOL isHaveCheck=checksumSet->isCheck_oldRefData||checksumSet->isCheck_newRefData||
                            checksumSet->isCheck_copyFileData||checksumSet->isCheck_dirDiffData;
    if (checksumSet->checksumPlugin){
        check(0==strcmp(self->dirDiffInfo.checksumType,checksumSet->checksumPlugin->checksumType()));
        check(self->dirDiffInfo.checksumByteSize==checksumSet->checksumPlugin->checksumByteSize());
    }else{// checksumPlugin == 0
        check(!isHaveCheck);
    }
    check(self->_pChecksumMem==0);//now unsupport reset
    if (isHaveCheck){
        size_t             checksumByteSize=self->dirDiffInfo.checksumByteSize;
        hpatch_StreamPos_t checksumOffset=self->dirDiffInfo.checksumOffset;
        self->_checksumSet=*checksumSet;
        self->_pChecksumMem=(TByte*)malloc(checksumByteSize*(4+1)); //+1 for checksumTemp
        check(self->_pChecksumMem!=0);
        check(self->_dirDiffData->read(self->_dirDiffData,checksumOffset,
                                       self->_pChecksumMem,self->_pChecksumMem+checksumByteSize*4));
        
        //checksum dirDiffData
        if (self->_checksumSet.isCheck_dirDiffData){
            TByte* checksumTemp=self->_pChecksumMem+checksumByteSize*4;
            if(!_do_checksum(self,self->_pChecksumMem+checksumByteSize*3,checksumTemp,self->_dirDiffData,
                             checksumOffset+checksumByteSize*3,checksumOffset+checksumByteSize*4))
                self->isDiffDataChecksumError=hpatch_TRUE;
            check(!self->isDiffDataChecksumError);
        }
    }

clear:
    return result;
}

hpatch_BOOL TDirPatcher_loadDirData(TDirPatcher* self,hpatch_TDecompress* decompressPlugin,
                                    const char* oldPath_utf8,const char* newPath_utf8){
    hpatch_BOOL result=hpatch_TRUE;
    size_t               memSize=0;
    TByte*               curMem=0;
    const _TDirDiffHead* head=&self->dirDiffHead;
    const size_t         pathSumSize=head->oldPathSumSize+head->newPathSumSize;
    TStreamCacheClip        headStream;
    _TDecompressInputSteram decompresser;
    TByte  temp_cache[hpatch_kStreamCacheSize];
    decompresser.decompressHandle=0;
    assert(self->_decompressPlugin==0);
    assert(self->_pDiffDataMem==0);
    self->_decompressPlugin=decompressPlugin;
    
    //dir head data clip stream
    {
        hpatch_StreamPos_t curPos=self->dirDiffHead.headDataOffset;
        check(getStreamClip(&headStream,&decompresser,self->dirDiffHead.headDataSize,
                            self->dirDiffHead.headDataCompressedSize,self->_dirDiffData,&curPos,
                            decompressPlugin,temp_cache,hpatch_kStreamCacheSize));
    }
    //mem
    memSize = (head->oldPathCount+head->newPathCount)*sizeof(const char*)
            + (head->oldRefFileCount+head->newRefFileCount)*sizeof(size_t)
            + head->sameFilePairCount*sizeof(hpatch_TSameFilePair)
            + (head->newRefFileCount)*sizeof(hpatch_StreamPos_t)
            + hpatch_kPathMaxSize*2 + pathSumSize;
    self->_pDiffDataMem=malloc(memSize);
    check(self->_pDiffDataMem!=0);
    curMem=self->_pDiffDataMem;
    self->newRefSizeList=(hpatch_StreamPos_t*)curMem;
        curMem+=head->newRefFileCount*sizeof(hpatch_StreamPos_t);
    self->oldUtf8PathList=(const char**)curMem; curMem+=head->oldPathCount*sizeof(const char*);
    self->newUtf8PathList=(const char**)curMem; curMem+=head->newPathCount*sizeof(const char*);
    self->oldRefList=(const size_t*)curMem; curMem+=head->oldRefFileCount*sizeof(size_t);
    self->newRefList=(const size_t*)curMem; curMem+=head->newRefFileCount*sizeof(size_t);
    self->dataSamePairList=(const hpatch_TSameFilePair*)curMem;
        curMem+=head->sameFilePairCount*sizeof(hpatch_TSameFilePair);
    
    self->_oldRootDir=(char*)curMem;
    self->_oldRootDir_bufEnd=self->_oldRootDir+hpatch_kPathMaxSize;  curMem+=hpatch_kPathMaxSize;
    self->_oldRootDir_end=pushDirPath(self->_oldRootDir,self->_oldRootDir_bufEnd,oldPath_utf8);
    check(0!=self->_oldRootDir_end);
    if (!self->dirDiffInfo.oldPathIsDir)
        --self->_oldRootDir_end; //without '/'
    self->_newRootDir=(char*)curMem;
    self->_newRootDir_bufEnd=self->_newRootDir+hpatch_kPathMaxSize;  curMem+=hpatch_kPathMaxSize;
    self->_newRootDir_end=pushDirPath(self->_newRootDir,self->_newRootDir_bufEnd,newPath_utf8);
    check(0!=self->_newRootDir_end);
    if (!self->dirDiffInfo.newPathIsDir)
        --self->_newRootDir_end;//without '/'
    //read old & new path List
    check(_TStreamCacheClip_readDataTo(&headStream,curMem,curMem+pathSumSize));
    formatDirTagForLoad((char*)curMem,(char*)curMem+pathSumSize);
    curMem+=pathSumSize;
    
    assert(curMem-memSize==self->_pDiffDataMem);
    check(clipCStrsTo((const char*)curMem-pathSumSize,(const char*)curMem,
                      (const char**)self->oldUtf8PathList,head->oldPathCount+head->newPathCount));
    //read oldRefList
    check(readIncListTo(&headStream,(size_t*)self->oldRefList,head->oldRefFileCount,head->oldPathCount));
    //read newRefList
    check(readIncListTo(&headStream,(size_t*)self->newRefList,head->newRefFileCount,head->newPathCount));
    //read newRefSizeList
    check(readListTo(&headStream,(hpatch_StreamPos_t*)self->newRefSizeList,head->newRefFileCount));
    //read dataSamePairList
    check(readSamePairListTo(&headStream,(hpatch_TSameFilePair*)self->dataSamePairList,head->sameFilePairCount,
                             head->newPathCount,head->oldPathCount));
    check(_TStreamCacheClip_isFinish(&headStream));
clear:
    if (decompresser.decompressHandle){
        if (!decompressPlugin->close(decompressPlugin,decompresser.decompressHandle))
            result=hpatch_FALSE;
        decompresser.decompressHandle=0;
    }
    return result;
}


hpatch_BOOL  _openRes(hpatch_IResHandle* res,hpatch_TStreamInput** out_stream){
    hpatch_BOOL  result=hpatch_TRUE;
    TDirPatcher*        self=(TDirPatcher*)res->resImport;
    size_t              resIndex=res-self->_resList;
    hpatch_TFileStreamInput*   file=self->_oldFileList+resIndex;
    const char*         utf8fileName=0;
    assert(resIndex<self->dirDiffHead.oldRefFileCount);
    assert(file->m_file==0);
    utf8fileName=TDirPatcher_getOldRefPathByRefIndex(self,resIndex);
    check(utf8fileName!=0);
    check(hpatch_TFileStreamInput_open(file,utf8fileName));
    *out_stream=&file->base;
clear:
    return result;
}
hpatch_BOOL _closeRes(hpatch_IResHandle* res,const hpatch_TStreamInput* stream){
    hpatch_BOOL  result=hpatch_TRUE;
    TDirPatcher*        self=(TDirPatcher*)res->resImport;
    size_t              resIndex=res-self->_resList;
    hpatch_TFileStreamInput*   file=self->_oldFileList+resIndex;
    assert(resIndex<self->dirDiffHead.oldRefFileCount);
    assert(file->m_file!=0);
    assert(stream==&file->base);
    check(hpatch_TFileStreamInput_close(file));
clear:
    return result;
}

hpatch_BOOL TDirPatcher_openOldRefAsStream(TDirPatcher* self,size_t kMaxOpenFileNumber,
                                           const hpatch_TStreamInput** out_oldRefStream){
    hpatch_BOOL result=hpatch_TRUE;
    size_t      refCount=self->dirDiffHead.oldRefFileCount;
    assert(self->_pOldRefMem==0);
    assert(self->_oldFileList==0);
    assert(self->_resLimit.streamList==0);
    assert(self->_oldRefStream.stream==0);
    {//open oldRef
        size_t      i;
        size_t      memSize=0;
        hpatch_StreamPos_t  sumFSize=0;
        assert(kMaxOpenFileNumber>=kMaxOpenFileNumber_limit_min);
        kMaxOpenFileNumber-=2;// for diffFile and one newFile
        //mem
        memSize=(sizeof(hpatch_IResHandle)+sizeof(hpatch_TFileStreamInput))*refCount;
        self->_pOldRefMem=malloc(memSize);
        check(self->_pOldRefMem!=0);
        self->_resList=(hpatch_IResHandle*)self->_pOldRefMem;
        self->_oldFileList=(hpatch_TFileStreamInput*)&self->_resList[refCount];
        memset(self->_resList,0,sizeof(hpatch_IResHandle)*refCount);
        memset(self->_oldFileList,0,sizeof(hpatch_TFileStreamInput)*refCount);
        
        //init
        for (i=0; i<refCount;++i){
            hpatch_TPathType          oldPathType;
            hpatch_StreamPos_t fileSize;
            const char* oldRefFileName=TDirPatcher_getOldRefPathByRefIndex(self,i);
            check(oldRefFileName!=0);
            check(hpatch_getPathTypeByName(oldRefFileName,&oldPathType,&fileSize));
            check(oldPathType==kPathType_file);
            self->_resList[i].resImport=self;
            self->_resList[i].resStreamSize=fileSize;
            self->_resList[i].open=_openRes;
            self->_resList[i].close=_closeRes;
            sumFSize+=fileSize;
        }
        check(sumFSize==self->dirDiffInfo.hdiffInfo.oldDataSize);
        //open
        check(hpatch_TResHandleLimit_open(&self->_resLimit,kMaxOpenFileNumber,self->_resList,refCount));
        check(hpatch_TRefStream_open(&self->_oldRefStream,self->_resLimit.streamList,self->_resLimit.streamCount));
        *out_oldRefStream=self->_oldRefStream.stream;
    }
clear:
    return result;
}

hpatch_BOOL TDirPatcher_closeOldRefStream(TDirPatcher* self){
    hpatch_BOOL result=hpatch_TRUE;
    hpatch_TRefStream_close(&self->_oldRefStream);
    result=hpatch_TResHandleLimit_close(&self->_resLimit);
    self->_oldRootDir=0;
    self->_oldRootDir_end=0;
    self->_oldRootDir_bufEnd=0;
    self->_resList=0;
    self->_oldFileList=0;
    if (self->_pOldRefMem){
        free(self->_pOldRefMem);
        self->_pOldRefMem=0;
    }
    return result;
}


static hpatch_BOOL _makeNewDir(hpatch_INewStreamListener* listener,size_t newPathIndex){
    hpatch_BOOL  result=hpatch_TRUE;
    TDirPatcher* self=(TDirPatcher*)listener->listenerImport;
    const char*  dirName=TDirPatcher_getNewPathByIndex(self,newPathIndex);
    check(dirName!=0);
    assert(hpatch_getIsDirName(dirName));
    check(self->_listener->makeNewDir(self->_listener,dirName));
clear:
    return result;
}
static hpatch_BOOL _copySameFile(hpatch_INewStreamListener* listener,size_t newPathIndex,size_t oldPathIndex){
    hpatch_BOOL  result=hpatch_TRUE;
    TDirPatcher* self=(TDirPatcher*)listener->listenerImport;
    const char*  oldFileName=0;
    const char*  newFileName=0;
    assert(oldPathIndex<self->dirDiffHead.oldPathCount);
    assert(newPathIndex<self->dirDiffHead.newPathCount);
    
    oldFileName=TDirPatcher_getOldPathByIndex(self,oldPathIndex);
    check(oldFileName!=0);
    newFileName=TDirPatcher_getNewPathByIndex(self,newPathIndex);
    check(newFileName!=0);
    check(self->_listener->copySameFile(self->_listener,oldFileName,newFileName,
                                        self->_checksumSet.isCheck_copyFileData?&self->_sameFileCopyListener:0));
clear:
    return result;
}
static hpatch_BOOL _openNewFile(hpatch_INewStreamListener* listener,size_t newRefIndex,
                                const hpatch_TStreamOutput** out_newFileStream){
    hpatch_BOOL  result=hpatch_TRUE;
    TDirPatcher* self=(TDirPatcher*)listener->listenerImport;
    const char*  utf8fileName=0;
    assert(newRefIndex<self->dirDiffHead.newRefFileCount);
    assert(self->_curNewFile->m_file==0);
    
    utf8fileName=TDirPatcher_getNewPathByIndex(self,self->newRefList[newRefIndex]);
    check(utf8fileName!=0);
    check(self->_listener->openNewFile(self->_listener,self->_curNewFile,
                                       utf8fileName,self->newRefSizeList[newRefIndex]));
    *out_newFileStream=&self->_curNewFile->base;
clear:
    return result;
}
static hpatch_BOOL _closeNewFile(hpatch_INewStreamListener* listener,const hpatch_TStreamOutput* newFileStream){
    hpatch_BOOL  result=hpatch_TRUE;
    TDirPatcher* self=(TDirPatcher*)listener->listenerImport;
    assert(self->_curNewFile!=0);
    assert(newFileStream==&self->_curNewFile->base);
    check(!self->_curNewFile->fileError);
    check(self->_listener->closeNewFile(self->_listener,self->_curNewFile));
    hpatch_TFileStreamOutput_init(self->_curNewFile);
clear:
    return result;
}

static void _writedNewRefData(struct hpatch_INewStreamListener* listener,const unsigned char* data,
                             const unsigned char* dataEnd){
    TDirPatcher* self=(TDirPatcher*)listener->listenerImport;
    if (self->_checksumSet.isCheck_newRefData)
        self->_checksumSet.checksumPlugin->append(self->_newRefChecksumHandle,data,dataEnd);
}

static hpatch_BOOL _do_checksumEnd(TDirPatcher* self,const TByte* checksumTest,TByte* checksumTemp,
                                   hpatch_checksumHandle* pcsHandle){
    size_t checksumByteSize=self->dirDiffInfo.checksumByteSize;
    hpatch_TChecksum*   checksumPlugin=self->_checksumSet.checksumPlugin;
    hpatch_checksumHandle* csHandle=*pcsHandle;
    assert(csHandle!=0);
    *pcsHandle=0;
    checksumPlugin->end(csHandle,checksumTemp,checksumTemp+checksumByteSize);
    checksumPlugin->close(checksumPlugin,csHandle);
    return (0==memcmp(checksumTest,checksumTemp,checksumByteSize));
}

static hpatch_BOOL _writedFinish(struct hpatch_INewStreamListener* listener){
    hpatch_BOOL  result=hpatch_TRUE;
    TDirPatcher* self=(TDirPatcher*)listener->listenerImport;
    size_t checksumByteSize=self->dirDiffInfo.checksumByteSize;
    //checksum newRefData end
    if (self->_checksumSet.isCheck_newRefData){
        if (!_do_checksumEnd(self,self->_pChecksumMem+checksumByteSize*1,
                              self->_pChecksumMem+checksumByteSize*4,&self->_newRefChecksumHandle))
            self->isNewRefDataChecksumError=hpatch_TRUE;
    }
    //checksum sameFileData end
    if (self->_checksumSet.isCheck_copyFileData){
        if (!_do_checksumEnd(self,self->_pChecksumMem+checksumByteSize*2,
                              self->_pChecksumMem+checksumByteSize*4,&self->_sameFileChecksumHandle))
            self->isCopyDataChecksumError=hpatch_TRUE;
    }
    check((!self->isNewRefDataChecksumError)&&(!self->isCopyDataChecksumError));
clear:
    return result;
}

static void _sameFile_copyedData(hpatch_ICopyDataListener* listener,const unsigned char* data,
                                 const unsigned char* dataEnd){
    TDirPatcher* self=(TDirPatcher*)listener->listenerImport;
    if (self->_checksumSet.isCheck_copyFileData)
        self->_checksumSet.checksumPlugin->append(self->_sameFileChecksumHandle,data,dataEnd);
}

hpatch_BOOL TDirPatcher_openNewDirAsStream(TDirPatcher* self,IDirPatchListener* listener,
                                           const hpatch_TStreamOutput** out_newDirStream){
    hpatch_BOOL result=hpatch_TRUE;
    size_t      refCount=self->dirDiffHead.newRefFileCount;
    assert(self->_pNewRefMem==0);
    assert(self->_newDirStream.stream==0);
    assert(self->_newDirStreamListener.listenerImport==0);
    self->_listener=listener;
    {//open new
        size_t      memSize=sizeof(hpatch_TFileStreamOutput);
        self->_pNewRefMem=malloc(memSize);
        check(self->_pNewRefMem!=0);
        self->_curNewFile=(hpatch_TFileStreamOutput*)self->_pNewRefMem;
        hpatch_TFileStreamOutput_init(self->_curNewFile);
        
        self->_newDirStreamListener.listenerImport=self;
        self->_newDirStreamListener.makeNewDir=_makeNewDir;
        self->_newDirStreamListener.copySameFile=_copySameFile;
        self->_newDirStreamListener.openNewFile=_openNewFile;
        self->_newDirStreamListener.closeNewFile=_closeNewFile;
        self->_newDirStreamListener.writedNewRefData=_writedNewRefData;
        self->_newDirStreamListener.writedFinish=_writedFinish;
        check(hpatch_TNewStream_open(&self->_newDirStream,&self->_newDirStreamListener,
                              self->dirDiffInfo.hdiffInfo.newDataSize,self->dirDiffHead.newPathCount,
                              self->newRefList,refCount,
                              self->dataSamePairList,self->dirDiffHead.sameFilePairCount));
        *out_newDirStream=self->_newDirStream.stream;
    }
    //checksum newRefData begin
    if (self->_checksumSet.isCheck_newRefData){
        hpatch_TChecksum*   checksumPlugin=self->_checksumSet.checksumPlugin;
        assert(self->_newRefChecksumHandle==0);
        self->_newRefChecksumHandle=checksumPlugin->open(checksumPlugin);
        check(self->_newRefChecksumHandle!=0);
        checksumPlugin->begin(self->_newRefChecksumHandle);
    }
    //checksum sameFileData begin
    if (self->_checksumSet.isCheck_copyFileData){
        hpatch_TChecksum*   checksumPlugin=self->_checksumSet.checksumPlugin;
        assert(self->_sameFileChecksumHandle==0);
        self->_sameFileChecksumHandle=checksumPlugin->open(checksumPlugin);
        check(self->_sameFileChecksumHandle!=0);
        checksumPlugin->begin(self->_sameFileChecksumHandle);
        
        self->_sameFileCopyListener.listenerImport=self;
        self->_sameFileCopyListener.copyedData=_sameFile_copyedData;
    }
clear:
    return result;
}

hpatch_BOOL TDirPatcher_closeNewDirStream(TDirPatcher* self){
    hpatch_BOOL result=hpatch_TNewStream_close(&self->_newDirStream);
    self->_newRootDir=0;
    self->_newRootDir_end=0;
    self->_newRootDir_bufEnd=0;
    self->_curNewFile=0;
    if (self->_pNewRefMem) {
        free(self->_pNewRefMem);
        self->_pNewRefMem=0;
    }
    return result;
}

hpatch_BOOL TDirPatcher_patch(TDirPatcher* self,const hpatch_TStreamOutput* out_newData,
                              const hpatch_TStreamInput* oldData,
                              TByte* temp_cache,TByte* temp_cache_end){
#define kPatchCacheSize_min      (hpatch_kStreamCacheSize*8)
    hpatch_BOOL         result=hpatch_TRUE;
    TStreamInputClip    hdiffData;
    hpatch_TStreamInput _cacheOldData;
    if (self->_checksumSet.isCheck_oldRefData){
        if ((size_t)(temp_cache_end-temp_cache)>=oldData->streamSize+kPatchCacheSize_min){
            //load all oldData into memory for optimize speed of checksum oldData
            size_t redLen=(size_t)oldData->streamSize;
            check(oldData->read(oldData,0,temp_cache,temp_cache+redLen));
            mem_as_hStreamInput(&_cacheOldData,temp_cache,temp_cache+redLen);
            temp_cache+=redLen;
            oldData=&_cacheOldData;
        }
        {//checksum oldRefData
            size_t checksumByteSize=self->dirDiffInfo.checksumByteSize;
            TByte* checksumTemp=self->_pChecksumMem+checksumByteSize*4;
            if(!_do_checksum(self,self->_pChecksumMem+checksumByteSize*0,checksumTemp,oldData,0,0)){
                self->isOldRefDataChecksumError=hpatch_TRUE;
            }
            check(!self->isOldRefDataChecksumError);
        }
    }
    TStreamInputClip_init(&hdiffData,self->_dirDiffData,self->dirDiffHead.hdiffDataOffset,
                          self->dirDiffHead.hdiffDataOffset+self->dirDiffHead.hdiffDataSize);
    check(patch_decompress_with_cache(out_newData,oldData,&hdiffData.base,
                                      self->_decompressPlugin,temp_cache,temp_cache_end));
clear:
    return result;
}

hpatch_BOOL TDirPatcher_close(TDirPatcher* self){
    hpatch_BOOL result=TDirPatcher_closeNewDirStream(self);
    result=TDirPatcher_closeOldRefStream(self) & result;
    TDirPatcher_finishOldSameRefCount(self);
    if (self->_pChecksumMem){
        hpatch_TChecksum*   checksumPlugin=self->_checksumSet.checksumPlugin;
        if (self->_newRefChecksumHandle){
            checksumPlugin->close(checksumPlugin,self->_newRefChecksumHandle);
            self->_newRefChecksumHandle=0;
        }
        if (self->_sameFileChecksumHandle){
            checksumPlugin->close(checksumPlugin,self->_sameFileChecksumHandle);
            self->_sameFileChecksumHandle=0;
        }
        free(self->_pChecksumMem);
        self->_pChecksumMem=0;
    }
    if (self->_pDiffDataMem){
        free(self->_pDiffDataMem);
        self->_pDiffDataMem=0;
    }
    return result;
}



const char* TDirPatcher_getOldPathByNewPath(TDirPatcher* self,const char* newPath){
    assert(self->_newRootDir==newPath);
    if (!addingPath(self->_oldRootDir_end,self->_oldRootDir_bufEnd,
                    self->_newRootDir_end)) return 0; //error
    return self->_oldRootDir;
}

const char* TDirPatcher_getOldPathByIndex(TDirPatcher* self,size_t oldPathIndex){
    assert(oldPathIndex<self->dirDiffHead.oldPathCount);
    if (!addingPath(self->_oldRootDir_end,self->_oldRootDir_bufEnd,
                    self->oldUtf8PathList[oldPathIndex])) return 0; //error
    return self->_oldRootDir;
}

const char* TDirPatcher_getOldRefPathByRefIndex(TDirPatcher* self,size_t oldRefIndex){
    assert(oldRefIndex<self->dirDiffHead.oldRefFileCount);
    return TDirPatcher_getOldPathByIndex(self,self->oldRefList[oldRefIndex]);
}


const char* TDirPatcher_getNewPathRoot(TDirPatcher* self){
    if (!addingPath(self->_newRootDir_end,self->_newRootDir_bufEnd,
                    "")) return 0; //error
    return self->_newRootDir;
}
const char* TDirPatcher_getNewPathByIndex(TDirPatcher* self,size_t newPathIndex){
    assert(newPathIndex<self->dirDiffHead.newPathCount);
    if (!addingPath(self->_newRootDir_end,self->_newRootDir_bufEnd,
                    self->newUtf8PathList[newPathIndex])) return 0; //error
    return self->_newRootDir;
}


hpatch_BOOL TDirPatcher_initOldSameRefCount(TDirPatcher* self){
    size_t* counts;
    size_t  i;
    size_t  memSize=sizeof(size_t)*self->dirDiffHead.oldPathCount;
    assert(self->_pOldSameRefCount==0);
    self->_pOldSameRefCount=(size_t*)malloc(memSize);
    if (self->_pOldSameRefCount==0) return hpatch_FALSE;
    
    counts=self->_pOldSameRefCount;
    memset(counts,0,memSize);
    for (i=0;i<self->dirDiffHead.sameFilePairCount; ++i) {
        size_t oldIndex=self->dataSamePairList[i].oldIndex;
        ++counts[oldIndex];
    }
    return hpatch_TRUE;
}
void  TDirPatcher_finishOldSameRefCount(TDirPatcher* self){
    if (self->_pOldSameRefCount!=0){
        free(self->_pOldSameRefCount);
        self->_pOldSameRefCount=0;
    }
}
const char* TDirPatcher_getOldPathBySameIndex(TDirPatcher* self,size_t sameIndex){
    assert(sameIndex<self->dirDiffHead.sameFilePairCount);
    return TDirPatcher_getOldPathByIndex(self,self->dataSamePairList[sameIndex].oldIndex);
}
const char* TDirPatcher_getNewPathBySameIndex(TDirPatcher* self,size_t sameIndex){
    assert(sameIndex<self->dirDiffHead.sameFilePairCount);
    return TDirPatcher_getNewPathByIndex(self,self->dataSamePairList[sameIndex].newIndex);
}
size_t TDirPatcher_oldSameRefCount(TDirPatcher* self,size_t sameIndex){
    size_t oldIndex;
    assert(sameIndex<self->dirDiffHead.sameFilePairCount);
    oldIndex=self->dataSamePairList[sameIndex].oldIndex;
    return self->_pOldSameRefCount[oldIndex];
}
void TDirPatcher_decOldSameRefCount(TDirPatcher* self,size_t sameIndex){
    size_t oldIndex;
    assert(sameIndex<self->dirDiffHead.sameFilePairCount);
    oldIndex=self->dataSamePairList[sameIndex].oldIndex;
    assert(self->_pOldSameRefCount[oldIndex]>0);
    --self->_pOldSameRefCount[oldIndex];
}

#endif

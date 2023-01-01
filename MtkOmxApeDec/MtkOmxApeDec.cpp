/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein is
 * confidential and proprietary to MediaTek Inc. and/or its licensors. Without
 * the prior written permission of MediaTek inc. and/or its licensors, any
 * reproduction, modification, use or disclosure of MediaTek Software, and
 * information contained herein, in whole or in part, shall be strictly
 * prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER
 * ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL
 * WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR
 * NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH
 * RESPECT TO THE SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY,
 * INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES
 * TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO.
 * RECEIVER EXPRESSLY ACKNOWLEDGES THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO
 * OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES CONTAINED IN MEDIATEK
 * SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE
 * RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S
 * ENTIRE AND CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE
 * RELEASED HEREUNDER WILL BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE
 * MEDIATEK SOFTWARE AT ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE
 * CHARGE PAID BY RECEIVER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek
 * Software") have been modified by MediaTek Inc. All revisions are subject to
 * any receiver's applicable license agreements with MediaTek Inc.
 */

//#define MTK_LOG_ENABLE 1
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include <dlfcn.h>
#include <cutils/log.h>
#include "osal_utils.h"
#include "MtkOmxApeDec.h"
#include <audio_utils/primitives.h>
#include <cutils/properties.h>

#undef LOG_TAG
#define LOG_TAG "MtkOmxApeDec"

#define MTK_OMX_APE_DECODER "OMX.MTK.AUDIO.DECODER.APE"

#define APE_ERR_EOS -1
#define APE_ERR_CRC -2

MtkOmxApeDec::MtkOmxApeDec() {
    SLOGD("MtkOmxApeDec::MtkOmxApeDec(APE) this= %p", this);
    mApeInitFlag = OMX_FALSE;//init flag

    mNewInBufferRequired = OMX_TRUE;
    mNewOutBufRequired = OMX_TRUE;
    mCurrentTime = 0;
    mSeekEnable = false;

    mInputApeParam.nPortIndex = MTK_OMX_INPUT_PORT;
    mInputApeParam.channels = 2;
    mInputApeParam.SampleRate = 48000;
    mInputApeParam.fileversion = 0;
    mInputApeParam.compressiontype = 0;
    mInputApeParam.blocksperframe = 0;
    mInputApeParam.finalframeblocks = 0;
    mInputApeParam.totalframes = 0;
    mInputApeParam.bps = 16;
    mInputApeParam.SourceBufferSize = 0;
    mInputApeParam.Bitrate = 0;
    mInputApeParam.seekfrm = 0;
    mInputApeParam.seekbyte = 0;

    mInsize = 0;
    mOutsize = 0;
    mWorkingBufferSize = 0;
    mApeHandle = NULL;
    mWorkingBuffer = NULL;    
    mTempBuffer = NULL;
    mTempBuffEnabled = false;
    mTempBuffFlag = false;
    mTempBufferSize = 0;
    mSourceRead = false;
    mTempOutBuffer = NULL;
    mConsumeBS = APE_ERR_EOS;
    status_t iresult = Create();
    ALOGD("create:%d",iresult);
}

MtkOmxApeDec::~MtkOmxApeDec() {
    mApeInitFlag = OMX_FALSE;
    mSeekEnable = false; 
    if (mApeHandle != NULL) {
        mApeHandle = NULL;

        if (mTempBuffer != NULL) {
            free(mTempBuffer);
            mTempBuffer = NULL;
        }

        if (mWorkingBuffer != NULL) {
            free(mWorkingBuffer);
            mWorkingBuffer = NULL;
        }
        if (mTempOutBuffer != NULL) {
            free(mTempOutBuffer);
            mTempOutBuffer = NULL;
        }
    }
    ALOGD("~dtor- this= %p", this);
}

status_t MtkOmxApeDec::Create() {
    ape_decoder_get_mem_size(&mInsize, &mWorkingBufferSize, &mOutsize);
    ALOGD("create:mInsize:%d,mWorkingBufferSize:%d,mOutsize:%d", mInsize, mWorkingBufferSize,
            mOutsize);
    mTempOutBuffer = (unsigned char*)malloc(mOutsize*sizeof(char));
    if (mTempOutBuffer == NULL) {
        ALOGE("mTempOutBuffer malloc failed for size: %u", mOutsize);
        return NO_MEMORY;
    }
    memset(mTempOutBuffer, 0, mOutsize);
    return OK;
}

status_t MtkOmxApeDec::Init() {
    if (mTempBuffer == NULL) {
        mTempBuffer = (unsigned char *)malloc(mInsize * sizeof(char));
        if (mTempBuffer == NULL) {
            ALOGE("mTempBuffer malloc failed");
            goto malloc_fail;
        }
    }
    if (mWorkingBuffer == NULL) {
        mWorkingBuffer = (unsigned char *)malloc(mWorkingBufferSize * sizeof(char));
        if (mWorkingBuffer == NULL) {
            ALOGE("mWorkingBuffer malloc failed");
            goto malloc_fail;
        }
    }
    if (mApeHandle == NULL) {
        mApeHandle = ape_decoder_init(mWorkingBuffer, &mApeInitParam);
        if (mApeHandle == NULL) {
            ALOGE("ape_decoder_init failed");
            return NO_INIT;
        }
    }
    mNewInBufferRequired = mNewOutBufRequired = OMX_TRUE;
    return OK;

malloc_fail:
    if (mTempBuffer != NULL) {
        free(mTempBuffer);
        mTempBuffer = NULL;
    }
    if (mWorkingBuffer != NULL) {
        free(mWorkingBuffer);
        mWorkingBuffer = NULL;
    }
    return NO_MEMORY;
}

status_t MtkOmxApeDec::DoCodec(OMX_BUFFERHEADERTYPE *pInputBuf, OMX_BUFFERHEADERTYPE *pOutputBuf) {
    int inOffset = pInputBuf->nOffset;
    int iflag = pInputBuf->nFlags;
    int inAlloclen = pInputBuf->nFilledLen;
    unsigned char * inputBuffer = pInputBuf->pBuffer;
    unsigned char * outputBuffer = pOutputBuf->pBuffer;
    ALOGV("Docodec+,inOffset:%d,iflag:%d,inAllocLen:%d", inOffset, iflag, inAlloclen);
    mApeConfig.inputBufferUsedLength = 0;
    mApeConfig.outputFrameSize = 0;
    if (mTempBuffEnabled == false) {
        ALOGV("tmpbuf enabled:F");
        if (mSourceRead == true) {
            ALOGD("buffer mSourceRead done in_offset %d, in_filllen %d", inOffset, inAlloclen);
            mApeConfig.pInputBuffer = (uint8_t *)mTempBuffer;
            memset(mTempBuffer, 0, mInsize);    
        } else {
            ALOGV("cfg.inputbuf=inpubuf+offset:%d", inOffset);    
            mApeConfig.pInputBuffer = (uint8_t *)inputBuffer + inOffset;    
        }
    } else {
        ALOGV("tmpbuf enabled:T");
        mApeConfig.pInputBuffer = (uint8_t *)mTempBuffer;
        if (mTempBuffFlag == true) {
            memcpy((void *)((OMX_U8 *)mTempBuffer + mTempBufferSize), inputBuffer,
                    mInsize- mTempBufferSize);
            mTempBuffFlag = false;
        }
    }
    mApeConfig.pOutputBuffer = outputBuffer;

    if (mApeInitParam.bps == 24) {
        mConsumeBS = ape_decoder_decode(mApeHandle, mApeConfig.pInputBuffer,
                (int *)&mApeConfig.inputBufferUsedLength, mTempOutBuffer,
                (int *)&mApeConfig.outputFrameSize);
        if (mApeConfig.outputFrameSize > 0) { //decode sucess
            float * fdest = (float *)mApeConfig.pOutputBuffer;
            int numSamples = mApeConfig.outputFrameSize / 3;
            ALOGD("fdest %p, mTempOutBuffer=%p, numSamples = %d, mConsumeBS = %d",
                    fdest, mTempOutBuffer, numSamples, mConsumeBS);
            memcpy_to_float_from_p24(fdest, (const uint8_t *)mTempOutBuffer, numSamples);
            mApeConfig.outputFrameSize = mApeConfig.outputFrameSize / 3 * sizeof(float);
        }
    } else {
        mConsumeBS = ape_decoder_decode(mApeHandle, mApeConfig.pInputBuffer,
                (int *)&mApeConfig.inputBufferUsedLength, mApeConfig.pOutputBuffer,
                (int *)&mApeConfig.outputFrameSize);
    }

    ALOGV("decode: mTempBuffEnabled %d, mConsumeBS %d,in_used %d out_len %d",
           mTempBuffEnabled, mConsumeBS, mApeConfig.inputBufferUsedLength,
           mApeConfig.outputFrameSize);
    
//mConsumeBS should be returned to the MtkOmxApeDec component...>>>
    if (mConsumeBS == APE_ERR_CRC) {
        ALOGD("APEDEC_INVALID_Frame CRC ERROR code %d", mConsumeBS);                    
        mApeConfig.outputFrameSize = 0;
        mNewInBufferRequired = OMX_TRUE;
        memset(mApeConfig.pOutputBuffer, 0, mApeConfig.outputFrameSize);
        mApeConfig.inputBufferUsedLength = 0;    
    } else if (mConsumeBS == APE_ERR_EOS) {
        mSourceRead = false;
        mNewInBufferRequired = OMX_FALSE;
        mNewOutBufRequired = OMX_FALSE;
        ALOGV("Decode Frame ERROR EOS");
    } else {
        mNewOutBufRequired = OMX_TRUE;
        if (mTempBuffEnabled == true) {
            ALOGV("tmpbuf true");
            mNewInBufferRequired = OMX_FALSE;
    //tempbuffer was decoded to end;why inputBufferUsedLength > mTempBufferSize?
            if (mApeConfig.inputBufferUsedLength >= (int)mTempBufferSize) {
                ALOGV("tmpbuf true:1,in used len:%d>temsize:%d", mApeConfig.inputBufferUsedLength,
                        mTempBufferSize);
                mApeConfig.inputBufferUsedLength -= mTempBufferSize;
                mTempBuffEnabled = false;
                mTempBufferSize = 0;
            } else {
                ALOGV("tmpbuf true:2");
                mTempBufferSize -= mApeConfig.inputBufferUsedLength;
                memmove(mTempBuffer,
                        (void *)((OMX_U8 *)mTempBuffer + mApeConfig.inputBufferUsedLength),
                        mTempBufferSize);
                if (mSourceRead == true) {
                    memset((void *)((OMX_U8 *)mTempBuffer + mTempBufferSize), 0,
                            mInsize- mTempBufferSize);
                } else {
                    memcpy((void *)((OMX_U8 *)mTempBuffer + mTempBufferSize), inputBuffer + inOffset,
                            mInsize - mTempBufferSize);
                }
                mApeConfig.inputBufferUsedLength = 0;
            }
        } else {
         //input buffer not docodec end,copy suplus data in input buffer to temp buffer
            ALOGV("tempbuf false:alloclen:%d,offset:%d,inUsedLen:%d",
                    inAlloclen, inOffset, mApeConfig.inputBufferUsedLength);
            if ((inAlloclen - inOffset - mApeConfig.inputBufferUsedLength) <= (int)mInsize) {
                ALOGV("tempbuf false1");
                mTempBuffEnabled = true;
                mTempBuffFlag = true;
                memset(mTempBuffer, 0, mInsize);
                
                if ((iflag & OMX_BUFFERFLAG_EOS)) {
                    mNewInBufferRequired = OMX_FALSE;
                    mSourceRead = true;
                } else {
                    mNewInBufferRequired = OMX_TRUE;
                }

                mTempBufferSize = inAlloclen - inOffset - mApeConfig.inputBufferUsedLength;
                memcpy(mTempBuffer,
                        (uint8_t *)(inputBuffer + inOffset + mApeConfig.inputBufferUsedLength),
                        mTempBufferSize);
            } else {
                ALOGV("tembuf false:2");
                mNewInBufferRequired = OMX_FALSE;
            }
        }
    }
    ALOGV("Docodec-,mConsumeBS:%d, outputFrameSize:%d, inputBufferUsedLength:%d,"
            "mNewInBufferRequired:%d, mNewOutBufRequired:%d",
            mConsumeBS, mApeConfig.outputFrameSize, mApeConfig.inputBufferUsedLength,
            mNewInBufferRequired, mNewOutBufRequired);
    return OK;
}

void MtkOmxApeDec::Reset(int seekbyte, int newframe) {
    ALOGV("reset+");
    ape_decoder_reset(mApeHandle, seekbyte, newframe);
    mTempBuffEnabled = false;
    mTempBuffFlag = false;
    mTempBufferSize = 0;
    //CR:ALPS01450062
    mSourceRead = false;
    if (mTempOutBuffer != NULL)
        memset(mTempOutBuffer, 0, mOutsize);
    ALOGV("reset-");
    return;
}

void MtkOmxApeDec::DecodeAudio(OMX_BUFFERHEADERTYPE *pInputBuf, OMX_BUFFERHEADERTYPE *pOutputBuf) {
    if (pInputBuf->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
        if (mApeInitFlag == OMX_FALSE) {  // just  init decoder only once
            if (OmxApeDecInit(pInputBuf)) {
                mApeInitFlag = OMX_TRUE;
                mOutputPcmMode.nSamplingRate = mInputApeParam.SampleRate;
                mOutputPcmMode.nChannels = mInputApeParam.channels;
            } else {
                fn_ErrHandle("DecodeAudio Init Failure!", OMX_ErrorBadParameter, 0,
                        pInputBuf, pOutputBuf, true);
                return;
            }
        }
        //empty csd buffer
        HandleEmptyBufferDone(pInputBuf);
        QueueOutputBuffer(findBufferHeaderIndex(MTK_OMX_OUTPUT_PORT, pOutputBuf));
        return;
    } else {
        // If EOS flag has come from the client & there are no more
        // input buffers to decode, send the callback to the client
        if (mInputApeParam.seekfrm != 0) {
            mSeekEnable = true;
        }
        ALOGV("decode+");
        if (mSignalledError == OMX_TRUE) {
            QueueInputBuffer(findBufferHeaderIndex(MTK_OMX_INPUT_PORT, pInputBuf));
            QueueOutputBuffer(findBufferHeaderIndex(MTK_OMX_OUTPUT_PORT, pOutputBuf));
            ALOGD("Error exit mNumPendingInput(%d), mNumPendingOutput(%d)", (int)mNumPendingInput,
                    (int)mNumPendingOutput);
            return;
        }
        if (mSeekEnable == true) {
            int32_t newframe = 255, seekbyte = 255;
            mSeekEnable = false;
            newframe = mInputApeParam.seekfrm;
            seekbyte = mInputApeParam.seekbyte;
            pOutputBuf->nTimeStamp = pInputBuf->nTimeStamp;
            mCurrentTime = pInputBuf->nTimeStamp;
            ALOGV("reset seekbyte=%d, newfrm=%d", seekbyte, newframe);
            Reset(seekbyte, newframe);
            mInputApeParam.seekfrm = 0;
            mInputApeParam.seekbyte = 0;
        }
        if (pInputBuf->nFlags & OMX_BUFFERFLAG_EOS) {
            ALOGD("APE EOS received, TS=%lld", pInputBuf->nTimeStamp);
        }
        if (pInputBuf->nOffset > pInputBuf->nAllocLen) {
            ALOGE("offset(%u) > allocLen(%u)", pInputBuf->nOffset, pInputBuf->nAllocLen);
            fn_ErrHandle("error, offset > alloclen", OMX_ErrorStreamCorrupt,
                    OMX_AUDIO_CodingAPE, pInputBuf, pOutputBuf, false);
            return;
        }

        /*****  Decode   *****/
        status_t iresult = DoCodec(pInputBuf, pOutputBuf);
        if (iresult != OK) {
            char pmsg[64]="";
            int len = sprintf(pmsg, "BnCodec error, result:%x", iresult);
            if (len > 0) {
                fn_ErrHandle(pmsg, OMX_ErrorNotImplemented, 0, pInputBuf, pOutputBuf, false);
            } else {
                ALOGE("BnCodec error, result:%x", iresult);
                fn_ErrHandle("BnCodec error", OMX_ErrorNotImplemented, 0, pInputBuf, pOutputBuf,
                        false);
            }
            return;
        }

//output param order: mConsumeBS, outputFrameSize,mNewInputBufferRequired,mNewOutBufRequired
        int32_t outFrameSize = mApeConfig.outputFrameSize;//reply.readInt32();
        int inputBufUsedLen = mApeConfig.inputBufferUsedLength;//reply.readInt32();
        ALOGV("mConsumeBS:%d,inputBufUsedLen:%d,outFrameSize:%d,mNewInBufferRequired:%d,"
                "mNewOutBufRequired:%d", mConsumeBS, inputBufUsedLen, outFrameSize,
                mNewInBufferRequired, mNewOutBufRequired);
        if (mConsumeBS == APE_ERR_CRC) {
            FlushAudioDecoder();
            char psMsg[128]="";
            int number = sprintf(psMsg, "CRC mNumPendingInput(%d), mNumPendingOutput(%d)",
                    (int)mNumPendingInput, (int)mNumPendingOutput);
            if (number > 0) {
                fn_ErrHandle(psMsg, OMX_ErrorStreamCorrupt, OMX_AUDIO_CodingAPE, pInputBuf,
                        pOutputBuf, false);
            } else {
                fn_ErrHandle("APE_ERR_CRC", OMX_ErrorStreamCorrupt, OMX_AUDIO_CodingAPE, pInputBuf,
                        pOutputBuf, false);
            }
            return;
        }
        else if (mConsumeBS == APE_ERR_EOS) {
            ALOGD("Decode Frame ERROR EOS");
            // This is recoverable, just ignore the current frame and play silence instead.
            FlushAudioDecoder();
            // return the EOS output buffer
            pOutputBuf->nFlags |= OMX_BUFFERFLAG_EOS;
            pOutputBuf->nTimeStamp = mCurrentTime;
            pInputBuf->nTimeStamp = mCurrentTime;
            pOutputBuf->nFilledLen = outFrameSize;
            HandleFillBufferDone(pOutputBuf);
            HandleEmptyBufferDone(pInputBuf);
            //flush input port,avoid to the last Input buffer eos cause decoder error
            FlushInputPort();
            ALOGD("File Send Eos mNumPendingInput(%d), mNumPendingOutput(%d)",
                    (int)mNumPendingInput, (int)mNumPendingOutput);
            return;
        } else {
            pOutputBuf->nTimeStamp = mCurrentTime;
            if (mOutputPcmMode.nBitPerSample == 32) {
                mCurrentTime += (outFrameSize * 1000000LL) /
                        (4 * mInputApeParam.channels * mInputApeParam.SampleRate);
            } else {
                mCurrentTime += (outFrameSize * 1000000) /
                        (2 * mInputApeParam.channels * mInputApeParam.SampleRate);
            }
        }
        pOutputBuf->nFilledLen = outFrameSize;
        pInputBuf->nOffset += inputBufUsedLen;
        if (mNewOutBufRequired) {
            ALOGV("new out buf req");
            HandleFillBufferDone(pOutputBuf);
        } else {
            ALOGV("que out buf");
            QueueOutputBuffer(findBufferHeaderIndex(MTK_OMX_OUTPUT_PORT, pOutputBuf));
        }

        if (mNewInBufferRequired) {
            ALOGV("new in buf req");
            HandleEmptyBufferDone(pInputBuf);
        } else {
            ALOGV("que in buf");
            QueueInputBuffer(findBufferHeaderIndex(MTK_OMX_INPUT_PORT, pInputBuf));
        }
        ALOGV("decode-");
    }
    return ;
}

void MtkOmxApeDec::fn_ErrHandle(const char *pErrMsg, int pErrType, int pCodingType,
        OMX_BUFFERHEADERTYPE *pInputBuf, OMX_BUFFERHEADERTYPE *pOutputBuf, bool isBufQue) {
    ALOGE("%s",pErrMsg);
    mSignalledError = OMX_TRUE;
    mCallback.EventHandler((OMX_HANDLETYPE)&mCompHandle, mAppData, OMX_EventError, pErrType,
            pCodingType, NULL);
    if (isBufQue) {
        QueueInputBuffer(findBufferHeaderIndex(MTK_OMX_INPUT_PORT, pInputBuf));
        QueueOutputBuffer(findBufferHeaderIndex(MTK_OMX_OUTPUT_PORT, pOutputBuf));
    } else {
        HandleEmptyBufferDone(pInputBuf);
        HandleFillBufferDone(pOutputBuf);
    }
}

void MtkOmxApeDec::FlushAudioDecoder() {
    mSeekEnable = true;
    mSignalledError = OMX_FALSE;
    SLOGD("MtkOmxApeDec::FlushAudioDecoder()");
}

void MtkOmxApeDec::DeinitAudioDecoder() {
    SLOGV("+DeinitAudioDecoder (APE)");

    mNewOutBufRequired = OMX_FALSE;
    mSeekEnable = false;
    SLOGV("-DeinitAudioDecoder (APE)");
}

OMX_BOOL MtkOmxApeDec::OmxApeDecInit(OMX_BUFFERHEADERTYPE * pInputBuf) {
    OMX_U8* pInBuffer = pInputBuf->pBuffer + pInputBuf->nOffset;
    mInputApeParam.fileversion = (OMX_S16)*((OMX_S16 *) (pInBuffer + 16));
    mInputApeParam.compressiontype = (OMX_U16)*((OMX_U16 *) (pInBuffer + 64));
    mInputApeParam.blocksperframe = (OMX_U32)*((OMX_U32 *) (pInBuffer + 68));
    mInputApeParam.finalframeblocks = (OMX_U32)*((OMX_U32 *) (pInBuffer + 72));
    mInputApeParam.totalframes = (OMX_U32)*((OMX_U32 *) (pInBuffer + 76));

    if (mInputApeParam.fileversion < 3950
            || mInputApeParam.fileversion > 4120
            || mInputApeParam.compressiontype > 4000
            || mInputApeParam.blocksperframe <= 0
            || mInputApeParam.totalframes <= 0
            || mInputApeParam.bps <= 0
            || mInputApeParam.SampleRate <= 0
            || mInputApeParam.SampleRate > 192000
            || mInputApeParam.channels <= 0
            || mInputApeParam.channels > 2) {
        SLOGD("APE header error: fileversion:%d,compressiontype:%d,blocksperframe %d,totalframes %d,"
                "bps %d,samplerate %d,channels:%d",
               mInputApeParam.fileversion,
               mInputApeParam.compressiontype,
               mInputApeParam.blocksperframe,
               mInputApeParam.totalframes,
               mInputApeParam.bps,
               mInputApeParam.SampleRate,
               mInputApeParam.channels);
        return OMX_FALSE;
    }
    mApeInitParam.blocksperframe   = mInputApeParam.blocksperframe;
    mApeInitParam.bps              = mInputApeParam.bps;
    mApeInitParam.channels         = mInputApeParam.channels;
    mApeInitParam.compressiontype  = mInputApeParam.compressiontype;
    mApeInitParam.fileversion      = mInputApeParam.fileversion;
    mApeInitParam.finalframeblocks = mInputApeParam.finalframeblocks;
    mApeInitParam.totalframes      = mInputApeParam.totalframes;
    ALOGD("Init,blocksperframe:%d, bps:%d, channels:%d, compressiontype:%d, fileversion:%d,"
            "finalframeblocks:%d, totalframes:%d",mInputApeParam.blocksperframe, mInputApeParam.bps,
            mInputApeParam.channels, mInputApeParam.compressiontype, mInputApeParam.fileversion,
            mInputApeParam.finalframeblocks, mInputApeParam.totalframes);
    status_t iresult = Init();
    if (iresult != OK) {
        return OMX_FALSE;
    } else {
        mNewOutBufRequired = OMX_TRUE;
        mSeekEnable = false;
        ALOGV("-MtkOmxApeDecInit sucess!");
        return OMX_TRUE;
    }
}

void  MtkOmxApeDec::QueueInputBuffer(int index) {
    LOCK(mEmptyThisBufQLock);

    SLOGV("@@ QueueInputBuffer (%d)", index);

#if ANDROID
    mEmptyThisBufQ.insertAt(index, 0);
#endif

    UNLOCK(mEmptyThisBufQLock);
}

void  MtkOmxApeDec::QueueOutputBuffer(int index) {
    LOCK(mFillThisBufQLock);

    SLOGV("@@ QueueOutputBuffer (%d)", index);

#if ANDROID
    mFillThisBufQ.insertAt(index, 0);
#endif
    SIGNAL(mDecodeSem);
    UNLOCK(mFillThisBufQLock);
}

// ComponentInit ==> InitAudioParams
OMX_BOOL MtkOmxApeDec::InitAudioParams() {
    SLOGD("MtkOmxApeDec::InitAudioParams(APE)");
    // init input port format
    strncpy((char *)mCompRole, "audio_decoder.ape", OMX_MAX_STRINGNAME_SIZE -1);
    mCompRole[OMX_MAX_STRINGNAME_SIZE -1] = '\0';
    mInputPortFormat.nPortIndex = MTK_OMX_INPUT_PORT;
    mInputPortFormat.nIndex = 0;
    mInputPortFormat.eEncoding = OMX_AUDIO_CodingAPE;

    // init output port format
    mOutputPortFormat.nPortIndex = MTK_OMX_OUTPUT_PORT;
    mOutputPortFormat.nIndex = 0;
    mOutputPortFormat.eEncoding = OMX_AUDIO_CodingPCM;

    // init input port definition
    mInputPortDef.nPortIndex = MTK_OMX_INPUT_PORT;
    mInputPortDef.eDir = OMX_DirInput;
    mInputPortDef.eDomain = OMX_PortDomainAudio;
    mInputPortDef.format.audio.pNativeRender = NULL;
    mInputPortDef.format.audio.cMIMEType = (OMX_STRING)"audio/mpeg";
    mInputPortDef.format.audio.bFlagErrorConcealment = OMX_FALSE;
    mInputPortDef.format.audio.eEncoding = OMX_AUDIO_CodingAPE;

    mInputPortDef.nBufferCountActual = MTK_OMX_NUMBER_INPUT_BUFFER_APE;
    mInputPortDef.nBufferCountMin = 2;
    mInputPortDef.nBufferSize = mInsize * 7;

    if (mInputPortDef.nBufferSize > 12288) {
        mInputPortDef.nBufferSize = 12288;    ///12k
    }

    mInputPortDef.bEnabled = OMX_TRUE;
    mInputPortDef.bPopulated = OMX_FALSE;

    //<---Donglei for conformance test
    mInputPortDef.bBuffersContiguous = OMX_FALSE;
    mInputPortDef.nBufferAlignment = OMX_FALSE;
    //--->

    // init output port definition
    mOutputPortDef.nPortIndex = MTK_OMX_OUTPUT_PORT;
    mOutputPortDef.eDomain = OMX_PortDomainAudio;
    mOutputPortDef.format.audio.cMIMEType = (OMX_STRING)"raw";
    mOutputPortDef.format.audio.pNativeRender = 0;
    mOutputPortDef.format.audio.bFlagErrorConcealment = OMX_FALSE;
    mOutputPortDef.format.audio.eEncoding = OMX_AUDIO_CodingPCM;
    mOutputPortDef.eDir = OMX_DirOutput;

    mOutputPortDef.nBufferCountActual = MTK_OMX_NUMBER_OUTPUT_BUFFER_APE;
    mOutputPortDef.nBufferCountMin = 1;
    ALOGV("InitAudioParams:mOutsize:%d",mOutsize);
    mOutputPortDef.nBufferSize = mOutsize;
    mOutputPortDef.bEnabled = OMX_TRUE;
    mOutputPortDef.bPopulated = OMX_FALSE;

    //<---Donglei for conformance test
    mOutputPortDef.bBuffersContiguous = OMX_FALSE;
    mOutputPortDef.nBufferAlignment = OMX_FALSE;
    //--->
    // init output pcm format
    mOutputPcmMode.nPortIndex = MTK_OMX_OUTPUT_PORT;
    mOutputPcmMode.nChannels = 2;
    mOutputPcmMode.eNumData = OMX_NumericalDataSigned;
    mOutputPcmMode.bInterleaved = OMX_TRUE;
    mOutputPcmMode.nBitPerSample = 16;
    mOutputPcmMode.nSamplingRate = 48000;//44100;by Changqing
    mOutputPcmMode.ePCMMode = OMX_AUDIO_PCMModeLinear;
    mOutputPcmMode.eChannelMapping[0] = OMX_AUDIO_ChannelLF;
    mOutputPcmMode.eChannelMapping[1] = OMX_AUDIO_ChannelRF;

    // allocate input buffer headers address array
    mInputBufferHdrs = (OMX_BUFFERHEADERTYPE **)MTK_OMX_ALLOC(sizeof(OMX_BUFFERHEADERTYPE *) *
        mInputPortDef.nBufferCountActual);
    if(NULL == mInputBufferHdrs) {
        ALOGE("malloc input buffer headers failed!!!");
        return OMX_FALSE;
    }
    MTK_OMX_MEMSET(mInputBufferHdrs, 0x00, sizeof(OMX_BUFFERHEADERTYPE *) *
            mInputPortDef.nBufferCountActual);

    // allocate output buffer headers address array
    mOutputBufferHdrs = (OMX_BUFFERHEADERTYPE **)MTK_OMX_ALLOC(sizeof(OMX_BUFFERHEADERTYPE *) *
        mOutputPortDef.nBufferCountActual);
    if(NULL == mOutputBufferHdrs) {
        ALOGE("malloc output buffer headers failed!!!");
        return OMX_FALSE;
    }
    MTK_OMX_MEMSET(mOutputBufferHdrs, 0x00, sizeof(OMX_BUFFERHEADERTYPE *) *
            mOutputPortDef.nBufferCountActual);

    return OMX_TRUE;
}

// Note: each MTK OMX component must export 'MtkOmxComponentCreate" to MtkOmxCore
extern "C" OMX_COMPONENTTYPE *MtkOmxComponentCreate(OMX_STRING componentName) {
    ALOGD("APE component create");
    // create component instance
    MtkOmxBase *pOmxApeDec  = new MtkOmxApeDec();
    if (NULL == pOmxApeDec) {
        SLOGE("MtkOmxComponentCreate out of memory!!!");
        return NULL;
    }
    // get OMX component handle
    OMX_COMPONENTTYPE *pHandle = pOmxApeDec->GetComponentHandle();
    SLOGV("MtkOmxComponentCreate mCompHandle(%p)", pOmxApeDec);
    // init component
    pOmxApeDec->ComponentInit(pHandle, componentName);
    return pHandle;
}

OMX_ERRORTYPE MtkOmxApeDec::SetParameter(OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_INDEXTYPE nParamIndex,
        OMX_IN OMX_PTR pCompParam) {
    OMX_ERRORTYPE err = OMX_ErrorNone;
    if(NULL == pCompParam) {
        err = OMX_ErrorBadParameter;
        goto EXIT;
    }
    if(mState == OMX_StateInvalid) {
        err = OMX_ErrorIncorrectStateOperation;
        goto EXIT;
    }
    switch(nParamIndex) {
    case OMX_IndexParamAudioApe: {
        ALOGV("SetParameter OMX_IndexParamAudioApe !!!");
        OMX_AUDIO_PARAM_APETYPE *pAudioApe = (OMX_AUDIO_PARAM_APETYPE *)pCompParam;
        if (pAudioApe->nPortIndex == mInputApeParam.nPortIndex) {
            memcpy(&mInputApeParam, pCompParam, sizeof(OMX_AUDIO_PARAM_APETYPE));
            ALOGV("setApeAudioParameter, channels(%d), sampleRate(%d), bps(%d)",
                    mInputApeParam.channels, mInputApeParam.SampleRate,
                    mInputApeParam.bps);
            mOutputPcmMode.nSamplingRate = mInputApeParam.SampleRate;
            mOutputPcmMode.nChannels = mInputApeParam.channels;
            if (mInputApeParam.bps == 24) {
                mOutputPcmMode.nBitPerSample = 32u;
                mOutputPcmMode.eNumData = OMX_NumericalDataFloat;
                mOutputPortDef.nBufferSize = mOutputPortDef.nBufferSize * sizeof (float) / 3;
                ALOGD("24bit--update ouput buffer size: %lld", (long long)mOutputPortDef.nBufferSize);
            }
        } else {
            err = OMX_ErrorUnsupportedIndex;
        }
        break;
        }

    default: {
        err = MtkOmxAudioDecBase::SetParameter(hComponent, nParamIndex, pCompParam);
        break;
        }
    }

EXIT:
    ALOGV("setParam, err = %ld", (long) err);
    return err;
}

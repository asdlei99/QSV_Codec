#include<stdio.h>
#include<windows.h>
/*
����QSVʵ��IntelӲ�����롣���õĽӿ���Intel Media SDK�ṩ�Ľӿڡ�
ע��:MFXVideoDECODE_DecodeFrameAsync����һ֡Packet����һ��һ����ɡ���Ҫͨ������ֵ��bitstream���ֽڳ��Ƚ����жϡ�
*/
extern "C"
{
#include "libavformat/avformat.h"
#include"mfx/mfxastructures.h"
#include"mfx/mfxsession.h"
#include"mfx/mfxvideo.h"
}
bool(__cdecl *get_h264_device_type)(int aiLastCodecType, BOOL lbIsEncode);//��ȡcodec����
void*(__cdecl *encoder_h264_create)(int aiCodecType);//��ȡ������
int(__cdecl *encoder_h264_setparam)(void *h, const char *param_name);//���ñ���������
int(__cdecl *setframeinfo_h264)(void *h, int aiFrameWidth, int aiFrameHeight, int aiBitCount);//���ñ������
int(__cdecl *encoder_h264_encode)(void *h, char *apSrcData, int aiSrcDataLen, char *apDstBuff, int *aiDstBuffLen, int *abMark);//����
void(__cdecl *encoder_h264_close)(void * h);//�رձ�����

void* (__cdecl *decoder_h264_create)(int aiCodecType);//��ȡ������
int(__cdecl *decoder_h264_decode)(void *h, char *apSrcData, int aiSrcDataLen, char *apDstBuff, int *aiDstBuffLen, int *abMark);//����
void(__cdecl *decoder_h264_close)(void *h);//�رս���

HMODULE			m_hCodec5Dll;          //VideoCodec5��DLL���	
FILE *pFile = nullptr;
int ig = 0;

BOOL InitVideoCodec5DLL()
{
	if (m_hCodec5Dll != NULL)
		return TRUE;

	m_hCodec5Dll = LoadLibrary(L"VideoCodec5.dll");
	if (m_hCodec5Dll == NULL)
	{
		m_hCodec5Dll = LoadLibrary(TEXT("VideoCodec5.dll"));
		if (m_hCodec5Dll == NULL)
		{
			return FALSE;
		}
	}

	get_h264_device_type =
		reinterpret_cast<bool(__cdecl*)(int, BOOL)>(GetProcAddress(m_hCodec5Dll, "get_h264_device_type"));
	encoder_h264_create =
		reinterpret_cast<void* (__cdecl*)(int)>(GetProcAddress(m_hCodec5Dll, "encoder_h264_create"));
	encoder_h264_setparam =
		reinterpret_cast<int(__cdecl*)(void*, const char *)>(GetProcAddress(m_hCodec5Dll, "encoder_h264_setparam"));
	setframeinfo_h264 =
		reinterpret_cast<int(__cdecl*)(void *, int, int, int)>(GetProcAddress(m_hCodec5Dll, "setframeinfo_h264"));
	encoder_h264_encode =
		reinterpret_cast<int(__cdecl*)(void *, char *, int, char *, int *, int *)>(GetProcAddress(m_hCodec5Dll, "encoder_h264_encode"));
	encoder_h264_close =
		reinterpret_cast<void(__cdecl*)(void*)>(GetProcAddress(m_hCodec5Dll, "encoder_h264_close"));
	decoder_h264_create =
		reinterpret_cast<void* (__cdecl*)(int)>(GetProcAddress(m_hCodec5Dll, "decoder_h264_create"));
	decoder_h264_decode =
		reinterpret_cast<int(__cdecl*)(void *, char *, int, char *, int *, int *)>(GetProcAddress(m_hCodec5Dll, "decoder_h264_decode"));
	decoder_h264_close =
		reinterpret_cast<void(__cdecl*)(void*)>(GetProcAddress(m_hCodec5Dll, "decoder_h264_close"));


	return TRUE;
}
#define QSV_VERSION_MAJOR 1
#define QSV_VERSION_MINOR 1

mfxSession session = nullptr;

bool InitMxfSession()
{
	mfxIMPL impl = MFX_IMPL_AUTO_ANY;
	mfxVersion ver = { { QSV_VERSION_MINOR, QSV_VERSION_MAJOR } };

	const char *desc;
	int ret;

	ret = MFXInit(impl, &ver, &session);
	if (ret < 0)
		return false;

	MFXQueryIMPL(session, &impl);

	switch (MFX_IMPL_BASETYPE(impl)) {
	case MFX_IMPL_SOFTWARE:
		desc = "software";
		break;
	case MFX_IMPL_HARDWARE:
	case MFX_IMPL_HARDWARE2:
	case MFX_IMPL_HARDWARE3:
	case MFX_IMPL_HARDWARE4:
		desc = "hardware accelerated";
		break;
	default:
		desc = "unknown";
	}
	/* make sure the decoder is uninitialized */
	MFXVideoDECODE_Close(session);

	return true;
}
mfxFrameInfo info;

bool InitCodec()
{
	mfxVideoParam param = { 0 };

	param.mfx.CodecId = MFX_CODEC_AVC;
	param.mfx.FrameInfo.BitDepthLuma = 8;
	param.mfx.FrameInfo.BitDepthChroma = 8;
	param.mfx.FrameInfo.Shift = 0;
	param.mfx.FrameInfo.FourCC = /*MFX_FOURCC_RGB4*/MFX_FOURCC_NV12;
	param.mfx.FrameInfo.Width =320;
	param.mfx.FrameInfo.Height = 240;
	param.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
	param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
	param.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
	param.AsyncDepth = 4;

	int ret = MFXVideoDECODE_Init(session, &param);
	if (ret < 0)
	{
		return false;
	}
	info = param.mfx.FrameInfo;

	return true;
}
//�����������ΪNV12��Ҳ����YUV420�����з�ʽΪY Y Y Y.... U V U V U V������Planer
bool Decode(AVPacket *avpkt)
{
	mfxFrameSurface1 *insurf = nullptr;
	mfxFrameSurface1 *outsurf = nullptr;
	mfxSyncPoint *sync = nullptr;
	mfxBitstream bs = { { { 0 } } };
	int ret;

	if (avpkt->size) {
		bs.Data = avpkt->data;
		bs.DataLength = avpkt->size;
		bs.MaxLength = bs.DataLength;
		bs.TimeStamp = avpkt->pts;
	}

	sync = (mfxSyncPoint *)av_malloc(sizeof(*sync));
	if (sync)
		memset(sync, 0, sizeof(*sync));

	do {

	
	mfxU8 *y = (mfxU8 *)new char[354799];//����ΪPitch ���Ը߶ȣ�Pitch��Ҫ32bit����
	mfxU8 *uv = (mfxU8*)new char[354799];
	memset(y,0,354799);
	memset(uv, 0, 354799);

	insurf = new mfxFrameSurface1;
	memset(insurf, 0, sizeof(mfxFrameSurface1));

	insurf->Info = info;
	insurf->Data.PitchLow = 736;	//32�ֽڶ��롣
	insurf->Data.Y =y;
	insurf->Data.UV = uv;


	ret = MFXVideoDECODE_DecodeFrameAsync(session, &bs,//��Ҫͨ��bs�ĳ�Ա�ж��Ƿ������ɣ�����һ֡���ݶ�ε��á�������Ҫÿ�ζ�Ҫ�µ�bs�ֲ����󡣲�Ȼ����ʾ�������޸�
		insurf, &outsurf, sync);

	} while (ret == MFX_WRN_DEVICE_BUSY || ret == MFX_ERR_MORE_SURFACE);
	
	if (*sync)
	{
		ig++;
		do {
			ret = MFXVideoCORE_SyncOperation(session, *sync, 1000);
		} while (ret == MFX_WRN_IN_EXECUTION);
		//outsurf->Data.Y
		for (int i = 0; i < outsurf->Info.CropH; i++)
		{
			fwrite(outsurf->Data.Y+i * outsurf->Data.Pitch, 1, outsurf->Info.CropW, pFile);//y
		}
		for (int i = 0; i <  outsurf->Info.CropH / 2; i++)//uv
		{
			for (int j = 0; j < outsurf->Info.CropW; j += 2)
			{
				fwrite(outsurf->Data.UV + i * outsurf->Data.Pitch + j, 1, 1, pFile);
			}
		}
		for (int i = 0; i < outsurf->Info.CropH / 2; i++)//uv
		{
			for (int j = 1; j < outsurf->Info.CropW; j += 2)
			{
				fwrite(outsurf->Data.UV + i * outsurf->Data.Pitch + j, 1, 1, pFile);
			}
		}
		if (ig == 20)
		{
			fclose(pFile);
		}
	}
	return true;
}
int main(int argc, char *argv[])
{
	av_register_all();
	
	AVFormatContext *pContext = nullptr;
	AVCodec *pCodec = nullptr;

	errno_t er = fopen_s(&pFile, "C:\\quanwei\\D1.yuv", "wb+");

	int ier = avformat_open_input(&pContext, "C:\\quanwei\\q_W320_H240_F33_Q100_ES.264", NULL, NULL);
	if (ier != 0)
	{
		printf("avformat_open_input Failed\n");
	}
	//Find Stream info
	ier = avformat_find_stream_info(pContext, nullptr);
	if (ier < 0) {
		printf("avformat_find_stream_info faile\n");
	}
	int nVtype = -1;
	ier = av_find_best_stream(pContext, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
	if (ier < 0)
	{
		printf("av_find_best_stream faile\n");
	}
	nVtype = ier;

	bool br = InitMxfSession();
	if (!br)
	{
		printf("");
	}

	br = InitCodec();
	if (br)
	{
		printf("");
	}

	////��ʼ��videocdec5
	//BOOL bre = InitVideoCodec5DLL();
	//if (!bre)
	//{
	//	printf("InitVideoCodec5DLL Failed\n");
	//}
	//HANDLE	m_pDecode = decoder_h264_create(1);
	//setframeinfo_h264(m_pDecode, 320, 240, 24);//

	AVPacket pk;

	int iMark = 1;
	int iLen = 320*240*3;
	char *pData = new char[iLen];
	memset(pData, 0, iLen);

	int i = 0;
	while (true)
	{
		ier = av_read_frame(pContext, &pk);
		if (ier == 0)
		{
			if (pk.stream_index == nVtype) {//Video
				Decode(&pk);
				//memset(pData, 0, iLen);
				//int ire = decoder_h264_decode(m_pDecode, (char*)pk.data, pk.size, pData, &iLen, &iMark);
				//if (ire)
				//{
				//	fwrite(pData, 1, iLen, pFile);
				//	i++;
				//	if (i == 10)
				//	{
				//		fclose(pFile);
				//		return 0;
				//	}
				//	printf("decode success\n");

				//}
			}
		}
		av_packet_unref(&pk);

	}
	
	return 0;
}
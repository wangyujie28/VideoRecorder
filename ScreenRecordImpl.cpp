#ifdef	__cplusplus
extern "C"
{
#endif
#include <libavcodec\avcodec.h>
#include <libavformat\avformat.h>
#include <libswscale\swscale.h>
#include <libavdevice\avdevice.h>
#include <libavutil\fifo.h>
#include <libavutil\imgutils.h>
#include <libswresample\swresample.h>
//#include 
#ifdef __cplusplus
};
#endif

#include "ScreenRecordImpl.h"
#include <QDebug>
#include <QFile>
#include <QFIleInfo>
#include <fstream>
#include <tlhelp32.h>
#include <process.h>
#include <QDateTime>
#include "ErrorDef.h"

using namespace std;

//g_collectFrameCnt����g_encodeFrameCnt֤�������֡��һ��
int g_collectFrameCnt = 0;	//�ɼ�֡��
int g_encodeFrameCnt = 0;	//����֡��

struct handle_data {
	unsigned long process_id;
	HWND best_handle;
};

BOOL IsMainWindow(HWND handle)
{
	return GetWindow(handle, GW_OWNER) == (HWND)0 && IsWindowVisible(handle);
}


BOOL CALLBACK EnumWindowsCallback(HWND handle, LPARAM lParam)
{
	handle_data& data = *(handle_data*)lParam;
	unsigned long process_id = 0;
	GetWindowThreadProcessId(handle, &process_id);
	if (data.process_id != process_id || !IsMainWindow(handle)) {
		return TRUE;
	}
	data.best_handle = handle;
	return FALSE;
}

HWND FindMainWindow(unsigned long process_id)
{
	handle_data data;
	data.process_id = process_id;
	data.best_handle = 0;
	EnumWindows(EnumWindowsCallback, (LPARAM)&data);
	return data.best_handle;
}


ScreenRecordImpl::ScreenRecordImpl(QObject * parent) :
	QObject(parent)
	, m_fps(25)
	, m_vIndex(-1)
	, m_vOutIndex(-1)
	, m_vFmtCtx(nullptr),m_oFmtCtx(nullptr)
	, m_vDecodeCtx(nullptr)
	, m_vEncodeCtx(nullptr)
	, m_dict(nullptr)
	, m_vFifoBuf(nullptr)
	, m_swsCtx(nullptr)
	, m_state(RecordState::NotStarted)
{
}

void ScreenRecordImpl::Init(const QVariantMap& map)
{
	m_filePath = map["filePath"].toString();
	m_width = map["width"].toInt();
	m_height = map["height"].toInt();
	m_fps = map["fps"].toInt();
}


int ScreenRecordImpl::Start(unsigned int pid, const QString& strSaveFileName, int width, int height, int fps)
{
	m_filePath = strSaveFileName;
	m_width = width;
	m_height = height;
	m_fps = fps;
	m_pid = pid;
	if (m_state == RecordState::NotStarted)
	{
		m_state = RecordState::Started;
		//std::thread recordThread(&ScreenRecordImpl::ScreenRecordThreadProc, this);
		//recordThread.detach();
		int rio = openIo();
		if(rio != 0)
			return rio;
		m_hScreenRecord = (HANDLE)_beginthreadex(nullptr, 0, &ScreenRecordImpl::ScreenRecordThreadProc, this, 0, nullptr);
		if (m_hScreenRecord == 0)
			return RECORD_ERROR_CREATE_THREAD;
	}
	return 0;
}

int ScreenRecordImpl::Resume()
{
	if (m_state == RecordState::Paused)
	{
		m_state = RecordState::Started;
		//m_cvNotPause.wakeAll();
	}
	return 0;
}

int ScreenRecordImpl::Pause()
{
	m_state = RecordState::Paused;
	return 0;
}

int ScreenRecordImpl::Stop()
{
	RecordState state = m_state;
	m_state = RecordState::Stopped;
	if(av_fifo_size(m_vFifoBuf) >= m_vOutFrameSize)
		m_cvNotEmpty.wakeOne();
	//if (state == RecordState::Paused)
	//	m_cvNotPause.wakeAll();
	HANDLE m_hEvent[2]; 
	m_hEvent[0] = m_hScreenRecord;
	m_hEvent[1] = m_hScreenAcquire;
	if( WAIT_OBJECT_0 != WaitForMultipleObjects(2, m_hEvent, TRUE, INFINITE))
		return RECORD_ERROR_STOP_THREAD;
	return 0;
}

int ScreenRecordImpl::openIo()
{
	av_register_all();
	avdevice_register_all();
	avcodec_register_all();
	if (OpenVideo() < 0)
		return RECORD_ERROR_OPEN_VIDEO;
	if (OpenOutput() < 0)
		return RECORD_ERROR_OPEN_OUTPUT;
	return InitBuffer();
}

int ScreenRecordImpl::OpenVideo()
{
	int ret = -1;
	AVInputFormat *ifmt = av_find_input_format("gdigrab");
	AVDictionary *options = nullptr;
	AVCodec *decoder = nullptr;
	//���òɼ�֡��
	HWND hwnd = FindMainWindow(m_pid);
	char szTitle[255] = {'\0'};
	setlocale(LC_ALL, "");
	DWORD gret = ::GetWindowText(hwnd, szTitle, sizeof(szTitle));
	if(gret <= 0)
		return -1;
	char szText[500];
	sprintf(szText, "title=%s", szTitle);
	av_dict_set(&options, "framerate", QString::number(m_fps).toStdString().c_str(), NULL);		
	if (avformat_open_input(&m_vFmtCtx, szText, ifmt, &options) != 0)
	{
		qDebug() << "Cant not open video input stream";
		return -1;
	}
	if (avformat_find_stream_info(m_vFmtCtx, nullptr) < 0)
	{
		qDebug() << "Couldn't find stream information";
		return -1;
	}
	for (int i = 0; i < m_vFmtCtx->nb_streams; ++i)
	{
		AVStream *stream = m_vFmtCtx->streams[i];
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			decoder = avcodec_find_decoder(stream->codecpar->codec_id);
			if (decoder == nullptr)
			{
				qDebug() << "avcodec_find_decoder failed";
				return -1;
			}
			//����Ƶ���п���������codecCtx
			m_vDecodeCtx = avcodec_alloc_context3(decoder);
			if ((ret = avcodec_parameters_to_context(m_vDecodeCtx, stream->codecpar)) < 0)
			{
				qDebug() << "Video avcodec_parameters_to_context failed,error code: " << ret;
				return -1;
			}
			m_vIndex = i;
			break;
		}
	}
	if (avcodec_open2(m_vDecodeCtx, decoder, &m_dict) < 0)
	{
		qDebug() << "avcodec_open2 failed";
		return -1;
	}
	m_swsCtx = sws_getContext(m_vDecodeCtx->width, m_vDecodeCtx->height, m_vDecodeCtx->pix_fmt,
		m_width, m_height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
	return 0;
}


int ScreenRecordImpl::OpenOutput()
{
	int ret = -1;
	AVStream *vStream = nullptr;
	string outFilePath = m_filePath.toStdString();
	QFileInfo fileInfo(m_filePath);
	ret = avformat_alloc_output_context2(&m_oFmtCtx, nullptr, nullptr, outFilePath.c_str());
	if (ret < 0)
	{
		qDebug() << "avformat_alloc_output_context2 failed";
		return -1;
	}
	if (m_vFmtCtx->streams[m_vIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
	{
		vStream = avformat_new_stream(m_oFmtCtx, nullptr);
		if (!vStream)
		{
			qDebug() << "can not new stream for output";
			return -1;
		}
		//AVFormatContext��һ����������������0���ڶ�����������������1
		m_vOutIndex = vStream->index;
		vStream->time_base.num = 1;
		vStream->time_base.den = m_fps;

		m_vEncodeCtx = avcodec_alloc_context3(NULL);
		if (nullptr == m_vEncodeCtx)
		{
			qDebug() << "avcodec_alloc_context3 failed";
			return -1;
		}

		//���ñ������
		SetEncoderParm();
		//������Ƶ������
		AVCodec *encoder;
		encoder = avcodec_find_encoder(m_vEncodeCtx->codec_id);
		if (!encoder)
		{
			qDebug() << "Can not find the encoder, id: " << m_vEncodeCtx->codec_id;
			return -1;
		}
		m_vEncodeCtx->codec_tag = 0;
		//��ȷ����sps/pps
		m_vEncodeCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		//����Ƶ������
		ret = avcodec_open2(m_vEncodeCtx, encoder, nullptr);
		if (ret < 0)
		{
			qDebug() << "Can not open encoder id: " << encoder->id << "error code: " << ret;
			return -1;
		}
		//��codecCtx�еĲ������������
		ret = avcodec_parameters_from_context(vStream->codecpar, m_vEncodeCtx);
		if (ret < 0)
		{
			qDebug() << "Output avcodec_parameters_from_context,error code:" << ret;
			return -1;
		}
	}
	//������ļ�
	if (!(m_oFmtCtx->oformat->flags & AVFMT_NOFILE))
	{
		if (avio_open(&m_oFmtCtx->pb, outFilePath.c_str(), AVIO_FLAG_WRITE) < 0)
		{
			qDebug() << "avio_open failed";
			return -1;
		}
	}
	//д�ļ�ͷ
	if (avformat_write_header(m_oFmtCtx, &m_dict) < 0)
	{
		qDebug() << "avformat_write_header failed";
		return -1;
	}
	return 0;
}

unsigned __stdcall ScreenRecordImpl::ScreenRecordThreadProc(void* pThis)
{
	int ret = -1;
	//��Сԭ�ӱ�������
	bool done = false;
	int vFrameIndex = 0;
	//av_register_all();
	//avdevice_register_all();
	//avcodec_register_all();
	ScreenRecordImpl* pTar = static_cast<ScreenRecordImpl*>(pThis);
	//if (pTar == nullptr)
	//	return 0;
	//if (pTar->OpenVideo() < 0)
	//	return 0;
	//if (pTar->OpenOutput() < 0)
	//	return 0;
	//pTar->InitBuffer();

	//������Ƶ���ݲɼ��߳�
	//std::thread screenRecord(&ScreenRecordImpl::ScreenAcquireThreadProc, this);
	//screenRecord.detach();
	pTar->m_hScreenAcquire = (HANDLE)_beginthreadex(nullptr, 0, &ScreenRecordImpl::ScreenAcquireThreadProc, pThis, 0, nullptr);
	if(pTar->m_hScreenAcquire == 0)
		return -1;
	while (1)
	{
		if (pTar->m_state == RecordState::Stopped && !done)
			done = true;
		if (done)
		{
			//lock_guard<mutex> lk(m_mtx);
			QMutexLocker locker(&pTar->m_mutex);
			if(av_fifo_size(pTar->m_vFifoBuf) < pTar->m_vOutFrameSize)
				break;
		}
		{
			//std::unique_lock<mutex> lk(m_mtx);
			QMutexLocker locker(&pTar->m_mutex);
			//m_cvNotEmpty.wait(lk, [this] {return av_fifo_size(m_vFifoBuf) >= m_vOutFrameSize; });
			if(av_fifo_size(pTar->m_vFifoBuf) < pTar->m_vOutFrameSize)
				pTar->m_cvNotEmpty.wait(&pTar->m_mutex);
		}
		av_fifo_generic_read(pTar->m_vFifoBuf, pTar->m_vOutFrameBuf, pTar->m_vOutFrameSize, NULL);
		//m_cvNotFull.notify_one();
		pTar->m_cvNotFull.wakeOne();
		//������Ƶ֡����
		//m_vOutFrame->pts = vFrameIndex * ((m_oFmtCtx->streams[m_vOutIndex]->time_base.den / m_oFmtCtx->streams[m_vOutIndex]->time_base.num) / m_fps);
		pTar->m_vOutFrame->pts = vFrameIndex;
		++vFrameIndex;
		pTar->m_vOutFrame->format = pTar->m_vEncodeCtx->pix_fmt;
		pTar->m_vOutFrame->width = pTar->m_width;
		pTar->m_vOutFrame->height = pTar->m_height;
		AVPacket pkt = { 0 };
		av_init_packet(&pkt);
		ret = avcodec_send_frame(pTar->m_vEncodeCtx, pTar->m_vOutFrame);
		if (ret != 0)
		{
			qDebug() << "video avcodec_send_frame failed, ret: " << ret;
			av_packet_unref(&pkt);
			Sleep(10);
			continue;
		}
		ret = avcodec_receive_packet(pTar->m_vEncodeCtx, &pkt);
		if (ret != 0)
		{
			av_packet_unref(&pkt);
			if (ret == AVERROR(EAGAIN))
			{
				qDebug() << "EAGAIN avcodec_receive_packet";
				continue;
			}
			qDebug() << "video avcodec_receive_packet failed, ret: " << ret;
			return 0;
		}
		pkt.stream_index = pTar->m_vOutIndex;
		av_packet_rescale_ts(&pkt, pTar->m_vEncodeCtx->time_base, pTar->m_oFmtCtx->streams[pTar->m_vOutIndex]->time_base);
		ret = av_interleaved_write_frame(pTar->m_oFmtCtx, &pkt);
		if (ret == 0)
		{
			//qDebug() << "Write video packet id: " << ++g_encodeFrameCnt;
		}
		else
		{
			qDebug() << "video av_interleaved_write_frame failed, ret:" << ret;
		}
		av_free_packet(&pkt);
	}
	pTar->FlushEncoder();
	av_write_trailer(pTar->m_oFmtCtx);
	pTar->Release();
	qDebug() << "parent thread exit";
	return 0;
}

unsigned __stdcall ScreenRecordImpl::ScreenAcquireThreadProc(void* pThis)
{
	ScreenRecordImpl* pTar = static_cast<ScreenRecordImpl*>(pThis);
	if (pTar == nullptr)
		return 0;
	int ret = -1;
	AVPacket pkt = { 0 };
	av_init_packet(&pkt);
	int y_size = pTar->m_width * pTar->m_height;
	AVFrame	*oldFrame = av_frame_alloc();
	AVFrame *newFrame = av_frame_alloc();

	int newFrameBufSize = av_image_get_buffer_size(pTar->m_vEncodeCtx->pix_fmt, pTar->m_width, pTar->m_height, 1);
	uint8_t *newFrameBuf = (uint8_t*)av_malloc(newFrameBufSize);
	av_image_fill_arrays(newFrame->data, newFrame->linesize, newFrameBuf,
		pTar->m_vEncodeCtx->pix_fmt, pTar->m_width, pTar->m_height, 1);

	while (pTar->m_state != RecordState::Stopped)
	{
		if (av_read_frame(pTar->m_vFmtCtx, &pkt) < 0)
		{
			qDebug() << "video av_read_frame < 0";
			Sleep(10);
			continue;
		}
		if (pTar->m_state == RecordState::Paused)
		{
			//unique_lock<mutex> lk(m_mtxPause);
			//QMutexLocker locker(&pTar->m_mutexPause);
			//m_cvNotPause.wait(lk, [this] { return pTar->m_state != RecordState::Paused; });
			//if(pTar->m_state == RecordState::Paused)
			//{
			//	pTar->m_cvNotPause.wait(&pTar->m_mutexPause);
			//}
			av_packet_unref(&pkt);
			Sleep(10);
			continue;
		}
		if (pkt.stream_index != pTar->m_vIndex)
		{
			qDebug() << "not a video packet from video input";
			av_packet_unref(&pkt);

		}
		ret = avcodec_send_packet(pTar->m_vDecodeCtx, &pkt);
		if (ret != 0)
		{
			qDebug() << "avcodec_send_packet failed, ret:" << ret;
			av_packet_unref(&pkt);
			continue;
		}
		ret = avcodec_receive_frame(pTar->m_vDecodeCtx, oldFrame);
		if (ret != 0)
		{
			qDebug() << "avcodec_receive_frame failed, ret:" << ret;
			av_packet_unref(&pkt);
			continue;
		}
		//QImage tmp((uchar*)(oldFrame->data[0]), m_vDecodeCtx->width, m_vDecodeCtx->height, QImage::Format_ARGB32);
		//tmp.save("1.jpg", "JPG");
		++g_collectFrameCnt;
		//srcSliceH����������߶Ȼ�������߶�
		sws_scale(pTar->m_swsCtx, (const uint8_t* const*)oldFrame->data, oldFrame->linesize, 0,
			pTar->m_vDecodeCtx->height, newFrame->data, newFrame->linesize);

		{

			//unique_lock<mutex> lk(m_mtx);
			QMutexLocker locker(&pTar->m_mutex);
			//m_cvNotFull.wait(lk, [this] { return av_fifo_space(pTar->m_vFifoBuf) >= pTar->m_vOutFrameSize; });
			if(av_fifo_space(pTar->m_vFifoBuf) < pTar->m_vOutFrameSize)
				pTar->m_cvNotFull.wait(&pTar->m_mutex);
		}
		//QImage tmp2((uchar*)(newFrame->data[0]), m_vDecodeCtx->width, m_vDecodeCtx->height, QImage::Format_Grayscale8);
		//tmp2.save("4.jpg", "JPG");
		av_fifo_generic_write(pTar->m_vFifoBuf, newFrame->data[0], y_size, NULL);
		av_fifo_generic_write(pTar->m_vFifoBuf, newFrame->data[1], y_size / 4, NULL);
		av_fifo_generic_write(pTar->m_vFifoBuf, newFrame->data[2], y_size / 4, NULL);
		pTar->m_cvNotEmpty.wakeOne();
		av_packet_unref(&pkt);
	}
	pTar->FlushDecoder();
	av_free(newFrameBuf);
	av_frame_free(&oldFrame);
	av_frame_free(&newFrame);
	qDebug() << "screen record thread exit";
	return 0;
}

void ScreenRecordImpl::SetEncoderParm()
{
	m_vEncodeCtx->width = m_width;
	m_vEncodeCtx->height = m_height;
	m_vEncodeCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	m_vEncodeCtx->time_base.num = 1;
	m_vEncodeCtx->time_base.den = m_fps;
	m_vEncodeCtx->pix_fmt = AV_PIX_FMT_YUV420P;

	QString suffix = QFileInfo(m_filePath).suffix();
	if (!QString::compare("mp4", suffix, Qt::CaseInsensitive) || !QString::compare("mkv", suffix, Qt::CaseInsensitive)
		|| !QString::compare("mov", suffix, Qt::CaseInsensitive))
	{
		m_vEncodeCtx->codec_id = AV_CODEC_ID_H264;
		m_vEncodeCtx->bit_rate = 1000 * 1000;
		m_vEncodeCtx->rc_max_rate = 1000 * 1000;
		//codec_ctx->rc_min_rate = 200 * 1000;
		m_vEncodeCtx->rc_buffer_size = 500 * 1000;
		/* ����ͼ�����Ĵ�С, gop_sizeԽ���ļ�ԽС */
		m_vEncodeCtx->gop_size = 12;
		m_vEncodeCtx->max_b_frames = 3;
		/* ����h264����صĲ��� */
		m_vEncodeCtx->qmin = 10;	//2
		m_vEncodeCtx->qmax = 31;	//31
		m_vEncodeCtx->max_qdiff = 4;
		m_vEncodeCtx->me_range = 16;	//0	
		m_vEncodeCtx->max_qdiff = 4;	//3	
		m_vEncodeCtx->qcompress = 0.6;	//0.5
		av_dict_set(&m_dict, "profile", "high", 0);
		// ͨ��--preset�Ĳ������ڱ����ٶȺ�������ƽ�⡣
		av_dict_set(&m_dict, "preset", "ultrafast", 0);
		//av_dict_set(&m_dict, "preset", "superfast", 0);
		av_dict_set(&m_dict, "threads", "0", 0);
		av_dict_set(&m_dict, "crf", "18", 0);
		// zerolatency: ���ӳ٣�������Ҫ�ǳ��͵��ӳٵ�����£�������ӵ绰����ı���
		av_dict_set(&m_dict, "tune", "zerolatency", 0);
		return;
	}
	else
	{
		m_vEncodeCtx->bit_rate = 4096 * 1000;
		if (!QString::compare("avi", suffix, Qt::CaseInsensitive))
			m_vEncodeCtx->codec_id = AV_CODEC_ID_MPEG4;
		else if (!QString::compare("wmv", suffix, Qt::CaseInsensitive))
			m_vEncodeCtx->codec_id = AV_CODEC_ID_MSMPEG4V3;
		else if (!QString::compare("flv", suffix, Qt::CaseInsensitive))
			m_vEncodeCtx->codec_id = AV_CODEC_ID_FLV1;
		else
			m_vEncodeCtx->codec_id = AV_CODEC_ID_MPEG4;
	}

}

void ScreenRecordImpl::FlushDecoder()
{
	int ret = -1;
	int y_size = m_width * m_height;
	AVFrame	*oldFrame = av_frame_alloc();
	AVFrame *newFrame = av_frame_alloc();

	ret = avcodec_send_packet(m_vDecodeCtx, nullptr);
	qDebug() << "flush avcodec_send_packet, ret: " << ret;
	while (ret >= 0)
	{
		ret = avcodec_receive_frame(m_vDecodeCtx, oldFrame);
		if (ret < 0)
		{
			if (ret == AVERROR(EAGAIN))
			{
				qDebug() << "flush EAGAIN avcodec_receive_frame";
				ret = 1;
				continue;
			}
			else if (ret == AVERROR_EOF)
			{
				qDebug() << "flush video decoder finished";
				break;
			}
			qDebug() << "flush video avcodec_receive_frame error, ret: " << ret;
			return;
		}
		++g_collectFrameCnt;
		sws_scale(m_swsCtx, (const uint8_t* const*)oldFrame->data, oldFrame->linesize, 0,
			m_vDecodeCtx->height, newFrame->data, newFrame->linesize);

		{

			//unique_lock<mutex> lk(m_mtx);
			//m_cvNotFull.wait(lk, [this] {return av_fifo_space(m_vFifoBuf) >= m_vOutFrameSize; });
			QMutexLocker locker(&m_mutex);
			if(av_fifo_space(m_vFifoBuf) < m_vOutFrameSize)
				m_cvNotFull.wait(&m_mutex);
		}
		av_fifo_generic_write(m_vFifoBuf, newFrame->data[0], y_size, NULL);
		av_fifo_generic_write(m_vFifoBuf, newFrame->data[1], y_size / 4, NULL);
		av_fifo_generic_write(m_vFifoBuf, newFrame->data[2], y_size / 4, NULL);
		m_cvNotEmpty.wakeOne();

	}
	qDebug() << "collect frame count: " << g_collectFrameCnt;
}

void ScreenRecordImpl::FlushEncoder()
{
	int ret = -1;
	AVPacket pkt = { 0 };
	av_init_packet(&pkt);
	ret = avcodec_send_frame(m_vEncodeCtx, nullptr);
	qDebug() << "avcodec_send_frame ret:" << ret;
	while (ret >= 0)
	{
		ret = avcodec_receive_packet(m_vEncodeCtx, &pkt);
		if (ret < 0)
		{
			av_packet_unref(&pkt);
			if (ret == AVERROR(EAGAIN))
			{
				qDebug() << "flush EAGAIN avcodec_receive_packet";
				ret = 1;
				continue;
			}
			else if (ret == AVERROR_EOF)
			{
				qDebug() << "flush video encoder finished";
				break;
			}
			qDebug() << "video avcodec_receive_packet failed, ret: " << ret;
			return;
		}
		//qDebug() << "flush succeed";
		pkt.stream_index = m_vOutIndex;
		av_packet_rescale_ts(&pkt, m_vEncodeCtx->time_base, m_oFmtCtx->streams[m_vOutIndex]->time_base);

		ret = av_interleaved_write_frame(m_oFmtCtx, &pkt);
		if (ret == 0)
		{
			qDebug() << "flush Write video packet id: " << ++g_encodeFrameCnt;
		}
		else
		{
			qDebug() << "video av_interleaved_write_frame failed, ret:" << ret;
		}
		av_free_packet(&pkt);
	}
}

int ScreenRecordImpl::InitBuffer()
{
	m_vOutFrameSize = av_image_get_buffer_size(m_vEncodeCtx->pix_fmt, m_width, m_height, 1);
	m_vOutFrameBuf = (uint8_t *)av_malloc(m_vOutFrameSize);
	m_vOutFrame = av_frame_alloc();
	//����AVFrameָ��ָ��buf��������д�����ݵ�buf
	av_image_fill_arrays(m_vOutFrame->data, m_vOutFrame->linesize, m_vOutFrameBuf, m_vEncodeCtx->pix_fmt, m_width, m_height, 1);
	//����30֡����
	if (!(m_vFifoBuf = av_fifo_alloc_array(30, m_vOutFrameSize)))
	{
		return RECORD_ERROR_INIT_BUFFER;
	}
	return 0;
}

void ScreenRecordImpl::Release()
{
	av_frame_free(&m_vOutFrame);
	av_free(m_vOutFrameBuf);

	if (m_vDecodeCtx)
	{
		avcodec_free_context(&m_vDecodeCtx);
		m_vDecodeCtx = nullptr;
	}
	if (m_vEncodeCtx)
	{
		avcodec_free_context(&m_vEncodeCtx);
		m_vEncodeCtx = nullptr;
	}
	if (m_vFifoBuf)
		av_fifo_freep(&m_vFifoBuf);
	if (m_vFmtCtx)
	{
		avformat_close_input(&m_vFmtCtx);
		m_vFmtCtx = nullptr;
	}
	avio_close(m_oFmtCtx->pb);
	avformat_free_context(m_oFmtCtx);
}

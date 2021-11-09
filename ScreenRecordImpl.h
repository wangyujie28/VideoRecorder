#pragma once


#include <Windows.h>
#include <QObject>
#include <QString>
#include <QMutex>
#include <QVariant>
#include <qmutex.h>
#include <QWaitCondition>
#include <stdint.h>

#ifdef	__cplusplus
extern "C"
{
#endif
struct AVFormatContext;
struct AVCodecContext;
struct AVCodec;
struct AVFifoBuffer;
struct AVAudioFifo;
struct AVFrame;
struct SwsContext;
struct SwrContext;
struct AVDictionary;
struct AVRational;
#ifdef __cplusplus
};
#endif


class ScreenRecordImpl : public QObject
{
	Q_OBJECT
private:
	enum RecordState {
		NotStarted,
		Started,
		Paused,
		Stopped,
		Unknown,
	};
public:

	ScreenRecordImpl(QObject * parent = nullptr);
	//初始化视频参数
	void Init(const QVariantMap& map);
	int openIo();

public slots :
	int Start(unsigned int pid, const QString& strSaveFileName, int width=1920, int height=1080, int fps=25);
	int Pause();
	int Stop();
	int Resume();

private:
	//从fifobuf读取视频帧，编码写入输出流，生成文件
	static unsigned __stdcall ScreenRecordThreadProc(void* pThis);
	//从视频输入流读取帧，写入fifobuf
	static unsigned __stdcall ScreenAcquireThreadProc(void* pThis);
	int OpenVideo();
	int OpenOutput();
	void SetEncoderParm();
	void FlushDecoder();
	void FlushEncoder();
	int InitBuffer();
	void Release();

private:
	HANDLE              m_hScreenRecord;
	HANDLE              m_hScreenAcquire;
	QString				m_filePath;
	int					m_width;
	int					m_height;
	int					m_fps;
	unsigned int        m_pid;
	int m_vIndex;		//输入视频流索引
	int m_vOutIndex;	//输出视频流索引
	AVFormatContext		*m_vFmtCtx;
	AVFormatContext		*m_oFmtCtx;
	AVCodecContext		*m_vDecodeCtx;
	AVCodecContext		*m_vEncodeCtx;
	AVDictionary		*m_dict;
	SwsContext			*m_swsCtx;
	AVFifoBuffer		*m_vFifoBuf;
	AVFrame				*m_vOutFrame;
	uint8_t				*m_vOutFrameBuf;
	int					m_vOutFrameSize;	//一个输出帧的字节
	RecordState			m_state;
	AVRational*         m_time_base;
	//编码速度一般比采集速度慢，所以可以去掉m_cvNotEmpty
	QWaitCondition m_cvNotFull;
	QWaitCondition m_cvNotEmpty;
	//std::condition_variable m_cvNotFull;	//当fifoBuf满了，采集线程挂起
	//std::condition_variable m_cvNotEmpty;	//当fifoBuf空了，编码线程挂起
	//std::mutex				m_mtx;			//m_cvNotFull和m_cvNotEmpty共用这个mutex
	QMutex m_mutex;
	//QWaitCondition m_cvNotPause;
	//std::condition_variable m_cvNotPause;	//当点击暂停的时候，采集线程挂起
	//std::mutex				m_mtxPause;
	//QMutex m_mutexPause;
};
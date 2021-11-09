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
	//��ʼ����Ƶ����
	void Init(const QVariantMap& map);
	int openIo();

public slots :
	int Start(unsigned int pid, const QString& strSaveFileName, int width=1920, int height=1080, int fps=25);
	int Pause();
	int Stop();
	int Resume();

private:
	//��fifobuf��ȡ��Ƶ֡������д��������������ļ�
	static unsigned __stdcall ScreenRecordThreadProc(void* pThis);
	//����Ƶ��������ȡ֡��д��fifobuf
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
	int m_vIndex;		//������Ƶ������
	int m_vOutIndex;	//�����Ƶ������
	AVFormatContext		*m_vFmtCtx;
	AVFormatContext		*m_oFmtCtx;
	AVCodecContext		*m_vDecodeCtx;
	AVCodecContext		*m_vEncodeCtx;
	AVDictionary		*m_dict;
	SwsContext			*m_swsCtx;
	AVFifoBuffer		*m_vFifoBuf;
	AVFrame				*m_vOutFrame;
	uint8_t				*m_vOutFrameBuf;
	int					m_vOutFrameSize;	//һ�����֡���ֽ�
	RecordState			m_state;
	AVRational*         m_time_base;
	//�����ٶ�һ��Ȳɼ��ٶ��������Կ���ȥ��m_cvNotEmpty
	QWaitCondition m_cvNotFull;
	QWaitCondition m_cvNotEmpty;
	//std::condition_variable m_cvNotFull;	//��fifoBuf���ˣ��ɼ��̹߳���
	//std::condition_variable m_cvNotEmpty;	//��fifoBuf���ˣ������̹߳���
	//std::mutex				m_mtx;			//m_cvNotFull��m_cvNotEmpty�������mutex
	QMutex m_mutex;
	//QWaitCondition m_cvNotPause;
	//std::condition_variable m_cvNotPause;	//�������ͣ��ʱ�򣬲ɼ��̹߳���
	//std::mutex				m_mtxPause;
	//QMutex m_mutexPause;
};
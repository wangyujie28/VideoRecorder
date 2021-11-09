#ifndef VIDEORECODER_H
#define VIDEORECODER_H

#include <QtCore/qglobal.h>
#include <QObject>
#include <QMap>


#ifdef VIDEORECODER_LIB
# define VIDEORECODER_EXPORT Q_DECL_EXPORT
#else
# define VIDEORECODER_EXPORT Q_DECL_IMPORT
#endif

class ScreenRecordImpl;

class VIDEORECODER_EXPORT VideoRecoder : public QObject
{
	Q_OBJECT
public:
	/*
	���ܣ���ʼ¼��Ƶ
	������pid  ���̵�id
	������strSaveFileName ������ļ�·�� ��D:/1.mp4
	������width  ����Ļ���Ŀ��
	������height  ����Ļ���ĸ߶�
	������fps     ֡��
	����ֵ��  0Ϊ�ɹ�  ����ֵΪʧ��
	*/
	int startRecord(unsigned int pid, const QString& strSaveFileName, int width=1920, int height=1080, int fps=25);
	/*
	���ܣ���ͣ¼��Ƶ
	������pid  ���̵�id
	����ֵ��  0Ϊ�ɹ�  ����ֵΪʧ��
	*/
	int puaseRecord(unsigned int pid);
	/*
	���ܣ�����¼��Ƶ
	������pid  ���̵�id
	����ֵ��  0Ϊ�ɹ�  ����ֵΪʧ��
	*/
	int stopRecord(unsigned int pid);
	/*
	���ܣ��ָ�¼��Ƶ
	������pid  ���̵�id
	����ֵ��  0Ϊ�ɹ�  ����ֵΪʧ��
	*/
	int resumeRecord(unsigned int pid);

private:
	VideoRecoder(QObject * parent=nullptr);
	~VideoRecoder();

public:
	/*
	���ܣ���ȡʵ��
	����ֵ��  ¼��ʵ��
	*/
	static VideoRecoder* getInstance();
private:
	static VideoRecoder* s_pInstance;
private:
	QMap<unsigned int, ScreenRecordImpl*> m_recoderImps;
};

#endif // VIDEORECODER_H

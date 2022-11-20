//------------------------------------------------------------------------
// Copyright(c) 2022 natas.
//------------------------------------------------------------------------

#include "ss_processor.h"
#include "ss_cids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "AudioFile.h"
#include "httplib.h"
#include <numeric>
#include <Query.h>
#include "params.h"
#include "json/json.h"
#include <windows.h>
#include <filesystem>
#include <chrono>
using namespace std::chrono;

long long func_get_timestamp() {
	return (duration_cast<milliseconds>(system_clock::now().time_since_epoch())).count();
}

using namespace Steinberg;

namespace MyCompanyName {
//------------------------------------------------------------------------
// NetProcessProcessor
//------------------------------------------------------------------------

	// �ز���
	int func_audio_resample(FUNC_SRC_SIMPLE dllFuncSrcSimple, float* fInBuffer, float* fOutBuffer, double src_ratio, long lInSize, long lOutSize) {
		SRC_DATA data;
		data.src_ratio = src_ratio;
		data.input_frames = lInSize;
		data.output_frames = lOutSize;
		data.data_in = fInBuffer;
		data.data_out = fOutBuffer;
		int error = dllFuncSrcSimple(&data, SRC_SINC_FASTEST, 1);
		return error;
	}

	// ���ڼ���һ����д���������Ч���ݴ�С
	long func_cacl_read_write_buffer_data_size(long lBufferSize, long lReadPos, long lWritePos) {
		long inputBufferSize;
		if (lReadPos < lWritePos) {
			inputBufferSize = lWritePos - lReadPos;
		}
		else {
			inputBufferSize = lWritePos + lBufferSize - lReadPos;
		}
		return inputBufferSize;
	}

	// boolֵ���л�Ϊ�ַ���
	std::string func_bool_to_string(bool bVal) {
		if (bVal) {
			return "true";
		} else {
			return "false";
		}
	}

		
	// ��������������Ϊ��ʱ���ڵ������߳�����У��������߳̿��ٱ���
	void func_do_voice_transfer_worker(
		int iNumberOfChanel,					// ͨ������
		double dProjectSampleRate,				// ��Ŀ������
		
		long lModelInputOutputBufferSize,		// ģ�����������������С
		float* fModeulInputSampleBuffer,		// ģ�����뻺����
		long* lModelInputSampleBufferReadPos,	// ģ�����뻺������ָ��
		long* lModelInputSampleBufferWritePos,	// ģ�����뻺����дָ��

		float* fModeulOutputSampleBuffer,		// ģ�����������
		long* lModelOutputSampleBufferReadPos,	// ģ�������������ָ��
		long* lModelOutputSampleBufferWritePos,	// ģ�����������дָ��

		float* fPrefixLength,					// ǰ��������ʱ��(s)
		float* fDropSuffixLength,				// ������β��ʱ��(s)
		float* fPitchChange,					// �����仯��ֵ
		bool* bCalcPitchError,					// �������������

		std::vector<roleStruct> roleStructList,	// ���õĿ�����ɫ�б�
		int* iSelectRoleIndex,					// ѡ��Ľ�ɫID
		FUNC_SRC_SIMPLE dllFuncSrcSimple,		// DLL�ڲ�SrcSimple����

		bool* bEnableSOVITSPreResample,			// ����SOVITSģ�������Ƶ�ز���Ԥ����
		int iSOVITSModelInputSamplerate,		// SOVITSģ����β�����
		bool* bEnableHUBERTPreResample,			// ����HUBERTģ�������Ƶ�ز���Ԥ����
		int iHUBERTInputSampleRate,				// HUBERTģ����β�����

		bool* bDisableVolumeDetect,				// ռλ����ͣ��������⣨��������ģʽ��
		bool* bFoundJit,						// ռλ�����Ƿ������Jitter����
		float fAvoidJitPrefixTime,				// Jitter�����ӵ�ǰ������������(s)
		bool* bDoItSignal						// ռλ������ʾ��worker�д����������
) {
	char buff[100];
	long long tTime1;
	long long tTime2;
	long long tUseTime;
	long long tStart;
	
	double dSOVITSInputSamplerate;
	char sSOVITSSamplerateBuff[100];
	char cPitchBuff[100];
	std::string sHUBERTSampleBuffer;
	std::string sCalcPitchError;
	std::string sEnablePreResample;

	while (true) {
		// ��ѵ����־λ
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		if (*bDoItSignal) {
			// ����Ҫ������źţ���ʼ����������־λ����Ϊfalse
			// �˴����ǵ��������⣬��δʹ�û�����ʵ��ԭ�Ӳ���
			// ��ͬһʱ���������Ҫ������źţ���ȵ���һ����־λΪtrueʱ�ٴ���Ҳ�޷�
			*bDoItSignal = false;
			tStart = func_get_timestamp();
			tTime1 = tStart;


			roleStruct roleStruct = roleStructList[*iSelectRoleIndex];

			// ������Ƶ���ݵ��ļ�
			// ��ȡ��ǰдָ���λ��
			long lTmpModelInputSampleBufferWritePos = *lModelInputSampleBufferWritePos;

			AudioFile<double>::AudioBuffer modelInputAudioBuffer;
			modelInputAudioBuffer.resize(iNumberOfChanel);

			// �Ӷ�����ȡ���������Ƶ����
			std::vector<float> vModelInputSampleBufferVector;
			if (*lModelInputSampleBufferReadPos < lTmpModelInputSampleBufferWritePos) {
				for (int i = *lModelInputSampleBufferReadPos; i < lTmpModelInputSampleBufferWritePos; i++) {
					vModelInputSampleBufferVector.push_back(fModeulInputSampleBuffer[i]);
				}
			}
			else {
				for (int i = *lModelInputSampleBufferReadPos; i < lModelInputOutputBufferSize; i++) {
					vModelInputSampleBufferVector.push_back(fModeulInputSampleBuffer[i]);
				}
				for (int i = 0; i < lTmpModelInputSampleBufferWritePos; i++) {
					vModelInputSampleBufferVector.push_back(fModeulInputSampleBuffer[i]);
				}
			}
			// ��ȡ��ϣ�����ָ��ָ�����дָ��
			*lModelInputSampleBufferReadPos = lTmpModelInputSampleBufferWritePos;


			if (*bEnableSOVITSPreResample) {
				// ��ǰ����Ƶ�ز�����C++�ز�����Python�˿�
				dSOVITSInputSamplerate = iSOVITSModelInputSamplerate;
				
				// SOVITS������Ƶ�ز���
				float* fReSampleInBuffer = vModelInputSampleBufferVector.data();
				float* fReSampleOutBuffer = fReSampleInBuffer;
				int iResampleNumbers = vModelInputSampleBufferVector.size();

				if (dProjectSampleRate != iSOVITSModelInputSamplerate) {
					double fScaleRate = iSOVITSModelInputSamplerate / dProjectSampleRate;
					iResampleNumbers = fScaleRate * iResampleNumbers;
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, vModelInputSampleBufferVector.size(), iResampleNumbers);
				}

				snprintf(sSOVITSSamplerateBuff, sizeof(sSOVITSSamplerateBuff), "%d", iSOVITSModelInputSamplerate);
				modelInputAudioBuffer[0].resize(iResampleNumbers);
				for (int i = 0; i < iResampleNumbers; i++) {
					modelInputAudioBuffer[0][i] = fReSampleOutBuffer[i];
				}

				if (*bEnableHUBERTPreResample) {
					// HUBERT������Ƶ�ز���
					double fScaleRate = iHUBERTInputSampleRate / dProjectSampleRate;
					iResampleNumbers = fScaleRate * vModelInputSampleBufferVector.size();
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, vModelInputSampleBufferVector.size(), iResampleNumbers);

					AudioFile<double>::AudioBuffer HUBERTModelInputAudioBuffer;
					HUBERTModelInputAudioBuffer.resize(iNumberOfChanel);
					HUBERTModelInputAudioBuffer[0].resize(iResampleNumbers);
					for (int i = 0; i < iResampleNumbers; i++) {
						HUBERTModelInputAudioBuffer[0][i] = fReSampleOutBuffer[i];
					}
					AudioFile<double> HUBERTAudioFile;
					HUBERTAudioFile.shouldLogErrorsToConsole(false);
					HUBERTAudioFile.setAudioBuffer(HUBERTModelInputAudioBuffer);
					HUBERTAudioFile.setAudioBufferSize(iNumberOfChanel, HUBERTModelInputAudioBuffer[0].size());
					HUBERTAudioFile.setBitDepth(24);
					HUBERTAudioFile.setSampleRate(iHUBERTInputSampleRate);

					// ������Ƶ�ļ����ڴ�
					std::vector<uint8_t> vHUBERTModelInputMemoryBuffer;
					HUBERTAudioFile.saveToWaveMemory(&vHUBERTModelInputMemoryBuffer);

					// ���ڴ��ȡ����
					auto vHUBERTModelInputData = vHUBERTModelInputMemoryBuffer.data();
					std::string sHUBERTModelInputString(vHUBERTModelInputData, vHUBERTModelInputData + vHUBERTModelInputMemoryBuffer.size());
					sHUBERTSampleBuffer = sHUBERTModelInputString;
				}
				else {
					sHUBERTSampleBuffer = "";
				}
			}
			else {
				// δ����Ԥ�����ز�������Ƶԭ������
				sHUBERTSampleBuffer = "";
				dSOVITSInputSamplerate = dProjectSampleRate;
				snprintf(sSOVITSSamplerateBuff, sizeof(sSOVITSSamplerateBuff), "%f", dProjectSampleRate);
				modelInputAudioBuffer[0].resize(vModelInputSampleBufferVector.size());
				for (int i = 0; i < vModelInputSampleBufferVector.size(); i++) {
					modelInputAudioBuffer[0][i] = vModelInputSampleBufferVector[i];
				}
			}

			int iModelInputNumSamples = modelInputAudioBuffer[0].size();
			AudioFile<double> audioFile;
			audioFile.shouldLogErrorsToConsole(false);
			audioFile.setAudioBuffer(modelInputAudioBuffer);
			audioFile.setAudioBufferSize(iNumberOfChanel, iModelInputNumSamples);
			audioFile.setBitDepth(24);
			audioFile.setSampleRate(dSOVITSInputSamplerate);
			
			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			snprintf(buff, sizeof(buff), "׼�����浽��Ƶ�ļ���ʱ:%lldms\n", tUseTime);
			OutputDebugStringA(buff);
			tTime1 = tTime2;

			// ������Ƶ�ļ����ڴ�
			std::vector<uint8_t> vModelInputMemoryBuffer;
			audioFile.saveToWaveMemory(&vModelInputMemoryBuffer);

			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			snprintf(buff, sizeof(buff), "���浽��Ƶ�ļ���ʱ:%lldms\n", tUseTime);
			OutputDebugStringA(buff);
			tTime1 = tTime2;

			// ����AIģ�ͽ�����������
			httplib::Client cli(roleStruct.sApiUrl);

			cli.set_connection_timeout(0, 1000000); // 300 milliseconds
			cli.set_read_timeout(5, 0); // 5 seconds
			cli.set_write_timeout(5, 0); // 5 seconds

			// ���ڴ��ȡ����
			auto vModelInputData = vModelInputMemoryBuffer.data();
			std::string sModelInputString(vModelInputData, vModelInputData + vModelInputMemoryBuffer.size());			

			// ׼��HTTP�������
			snprintf(cPitchBuff, sizeof(cPitchBuff), "%f", *fPitchChange);
			sCalcPitchError = func_bool_to_string(*bCalcPitchError);
			sEnablePreResample = func_bool_to_string(*bEnableSOVITSPreResample);

			httplib::MultipartFormDataItems items = {
				{ "sSpeakId", roleStruct.sSpeakId, "", ""},
				{ "sName", roleStruct.sName, "", ""},
				{ "fPitchChange", cPitchBuff, "", ""},
				{ "sampleRate", sSOVITSSamplerateBuff, "", ""},
				{ "bCalcPitchError", sCalcPitchError.c_str(), "", ""},
				{ "bEnablePreResample", sEnablePreResample.c_str(), "", ""},
				{ "sample", sModelInputString, "sample.wav", "audio/x-wav"},
				{ "hubert_sample", sHUBERTSampleBuffer, "hubert_sample.wav", "audio/x-wav"},
			};
			OutputDebugStringA("����AI�㷨ģ��\n");
			auto res = cli.Post("/voiceChangeModel", items);

			tTime2 = func_get_timestamp();
			tUseTime = tTime2 - tTime1;
			snprintf(buff, sizeof(buff), "����HTTP�ӿں�ʱ:%lldms\n", tUseTime);
			OutputDebugStringA(buff);
			tTime1 = tTime2;

			if (res.error() == httplib::Error::Success && res->status == 200) {
				// ���óɹ�����ʼ��������뵽��ʱ�����������滻���
				std::string body = res->body;
				std::vector<uint8_t> vModelOutputBuffer(body.begin(), body.end());

				AudioFile<double> tmpAudioFile;
				tmpAudioFile.loadFromMemory(vModelOutputBuffer);
				int sampleRate = tmpAudioFile.getSampleRate();
				// int bitDepth = tmpAudioFile.getBitDepth();
				int numSamples = tmpAudioFile.getNumSamplesPerChannel();
				double lengthInSeconds = tmpAudioFile.getLengthInSeconds();
				// int numChannels = tmpAudioFile.getNumChannels();
				bool isMono = tmpAudioFile.isMono();

				// ��Ƶ��ʽ����
				// ��1s���������Ƕ������0.1s��ȡ�����������ƴ��
				
				// ����ǰ����Ƶ�ź�
				int iSkipSamplePosStart = *fPrefixLength * sampleRate;
				// ����β���ź�
				int iSkipSamplePosEnd = numSamples - (*fDropSuffixLength * sampleRate);
				int iSliceSampleNumber = iSkipSamplePosEnd - iSkipSamplePosStart;
				float* fSliceSampleBuffer = (float*)(std::malloc(sizeof(float) * iSliceSampleNumber));
				int iSlicePos = 0;
				auto fOriginAudioBuffer = tmpAudioFile.samples[0];
				for (int i = iSkipSamplePosStart; i < iSkipSamplePosEnd; i++) {
					fSliceSampleBuffer[iSlicePos++] = fOriginAudioBuffer[i];
				}

				// ��Ƶ�ز���
				float* fReSampleInBuffer = (float*)malloc(iSliceSampleNumber * sizeof(float));
				float* fReSampleOutBuffer = fReSampleInBuffer;
				int iResampleNumbers = iSliceSampleNumber;
				for (int i = 0; i < iSliceSampleNumber; i++) {
					fReSampleInBuffer[i] = fSliceSampleBuffer[i];
				}
				if (sampleRate != dProjectSampleRate) {
					double fScaleRate = dProjectSampleRate / sampleRate;
					iResampleNumbers = fScaleRate * iSliceSampleNumber;
					fReSampleOutBuffer = (float*)(std::malloc(sizeof(float) * iResampleNumbers));
					func_audio_resample(dllFuncSrcSimple, fReSampleInBuffer, fReSampleOutBuffer, fScaleRate, iSliceSampleNumber, iResampleNumbers);
				}

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				snprintf(buff, sizeof(buff), "��ģ������ز�����ʱ:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
				tTime1 = tTime2;

				long lTmpModelOutputSampleBufferWritePos = *lModelOutputSampleBufferWritePos;
				
				// Ϊ�˱���ģ��JIT��дһ�ξ����Ļ�����
				if (*bFoundJit && *bDisableVolumeDetect) {
					*bFoundJit = false;
					int iRepeatSilenceSampleNumber = dProjectSampleRate * fAvoidJitPrefixTime;
					for (int i = 0; i < iRepeatSilenceSampleNumber; i++) {
						fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = 0.f;
						if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
							lTmpModelOutputSampleBufferWritePos = 0;
						}
					}
				}
				for (int i = 0; i < iResampleNumbers; i++) {
					fModeulOutputSampleBuffer[lTmpModelOutputSampleBufferWritePos++] = fReSampleOutBuffer[i];
					if (lTmpModelOutputSampleBufferWritePos == lModelInputOutputBufferSize) {
						lTmpModelOutputSampleBufferWritePos = 0;
					}
					// ע�⣬��Ϊ������Ӧ�������ܵĴ����Դ˴�������дָ��׷�϶�ָ������
				}
				// ��дָ��ָ���µ�λ��
				*lModelOutputSampleBufferWritePos = lTmpModelOutputSampleBufferWritePos;
				snprintf(buff, sizeof(buff), "���дָ��:%ld\n", lTmpModelOutputSampleBufferWritePos);
				OutputDebugStringA(buff);

				tTime2 = func_get_timestamp();
				tUseTime = tTime2 - tTime1;
				snprintf(buff, sizeof(buff), "д��������ʱ:%lldms\n", tUseTime);
				OutputDebugStringA(buff);
				tTime1 = tTime2;
			}
			else {
				auto err = res.error();
				snprintf(buff, sizeof(buff), "�㷨�������:%d\n", err);
				OutputDebugStringA(buff);
			}
			tUseTime = func_get_timestamp() - tStart;
			snprintf(buff, sizeof(buff), "�ô�woker��ѵ�ܺ�ʱ:%lld\n", tUseTime);
			OutputDebugStringA(buff);
		}
	}
}

NetProcessProcessor::NetProcessProcessor()
	: kRecordState(IDLE)
	, fRecordIdleTime(0.f)
	// Ĭ��ֻ��������
	, iNumberOfChanel(1)
	, bCalcPitchError(defaultEnabelPitchErrorCalc)
	, fMaxSliceLength(2.f)
	, fPitchChange(0.f)
	, dllFuncSrcSimple(nullptr){
	//--- set the wanted controller for our processor
	setControllerClass (kNetProcessControllerUID);
}

//------------------------------------------------------------------------
NetProcessProcessor::~NetProcessProcessor ()
{}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::initialize (FUnknown* context)
{
	std::wstring sDllDir = L"C:/Program Files/Common Files/VST3/NetProcess.vst3/Contents/x86_64-win";
	AddDllDirectory(sDllDir.c_str());
	sDllDir = L"C:/Windows/SysWOW64";
	AddDllDirectory(sDllDir.c_str());
	sDllDir = L"C:/temp/vst";
	AddDllDirectory(sDllDir.c_str());
	sDllDir = L"C:/temp/vst3";
	AddDllDirectory(sDllDir.c_str());

	std::wstring sDllPath = L"C:/Program Files/Common Files/VST3/NetProcess.vst3/Contents/x86_64-win/samplerate.dll";
	auto dllClient = LoadLibrary(sDllPath.c_str());
	if (dllClient != NULL) {
		dllFuncSrcSimple = (FUNC_SRC_SIMPLE)GetProcAddress(dllClient, "src_simple");
	}
	else {
		OutputDebugStringA("samplerate.dll load Error!");
	}

	// ��ȡJSON�����ļ�
	Json::Value jsonRoot;
	std::ifstream t_pc_file(sJsonConfigFileName, std::ios::binary);
	std::stringstream buffer_pc_file;
	buffer_pc_file << t_pc_file.rdbuf();
	buffer_pc_file >> jsonRoot;

	bDisableVolumeDetect = jsonRoot["bDisableVolumeDetect"].asBool();
	bEnableSOVITSPreResample = jsonRoot["bEnableSOVITSPreResample"].asBool();
	iSOVITSModelInputSamplerate = jsonRoot["iSOVITSModelInputSamplerate"].asInt();
	bEnableHUBERTPreResample = jsonRoot["bEnableHUBERTPreResample"].asBool();
	iHUBERTInputSampleRate = jsonRoot["iHUBERTInputSampleRate"].asInt();
	fAvoidJitPrefixTime = jsonRoot["fAvoidJitPrefixTime"].asFloat();
	fLowVolumeDetectTime = jsonRoot["fLowVolumeDetectTime"].asFloat();
	fSampleVolumeWorkActiveVal = jsonRoot["fSampleVolumeWorkActiveVal"].asDouble();
	fPrefixLength = jsonRoot["fPrefixLength"].asFloat();
	fDropSuffixLength = jsonRoot["fDropSuffixLength"].asFloat();

	roleList.clear();
	int iRoleSize = jsonRoot["roleList"].size();
	for (int i = 0; i < iRoleSize; i++) {
		std::string apiUrl = jsonRoot["roleList"][i]["apiUrl"].asString();
		std::string name = jsonRoot["roleList"][i]["name"].asString();
		std::string speakId = jsonRoot["roleList"][i]["speakId"].asString();
		roleStruct role;
		role.sSpeakId = speakId;
		role.sName = name;
		role.sApiUrl = apiUrl;
		roleList.push_back(role);
	}
	iSelectRoleIndex = 0;

	// ǰ�������������ʼ��
	lPrefixLengthSampleNumber = fPrefixLength * this->processSetup.sampleRate;
	fPrefixBuffer = (float*)std::malloc(sizeof(float) * lPrefixLengthSampleNumber);
	lPrefixBufferPos = 0;
	lNoOutputCount = 0;
	bFoundJit = true;
	bDoItSignal = false;

	// ��ʼ���̼߳佻�����ݵĻ�������120s�Ļ������㹻��
	float fModelInputOutputBufferSecond = 120.f;
	lModelInputOutputBufferSize = fModelInputOutputBufferSecond * this->processSetup.sampleRate;
	fModeulInputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelInputOutputBufferSize));
	fModeulOutputSampleBuffer = (float*)(std::malloc(sizeof(float) * lModelInputOutputBufferSize));
	lModelInputSampleBufferReadPos = 0;
	lModelInputSampleBufferWritePos = 0;
	lModelOutputSampleBufferReadPos = 0;
	lModelOutputSampleBufferWritePos = 0;

	// ����Worker�߳�
	std::thread (func_do_voice_transfer_worker,
				iNumberOfChanel,					// ͨ������
				this->processSetup.sampleRate,		// ��Ŀ������

				lModelInputOutputBufferSize,		// ģ�����������������С
				fModeulInputSampleBuffer,			// ģ�����뻺����
				&lModelInputSampleBufferReadPos,	// ģ�����뻺������ָ��
				&lModelInputSampleBufferWritePos,	// ģ�����뻺����дָ��

				fModeulOutputSampleBuffer,			// ģ�����������
				&lModelOutputSampleBufferReadPos,	// ģ�������������ָ��
				&lModelOutputSampleBufferWritePos,	// ģ�����������дָ��

				&fPrefixLength,						// ǰ��������ʱ��(s)
				&fDropSuffixLength,					// ������β��ʱ��(s)
				&fPitchChange,						// �����仯��ֵ
				&bCalcPitchError,					// �������������

				roleList,							// ���õĿ�����ɫ�б�
				&iSelectRoleIndex,					// ѡ��Ľ�ɫID
				dllFuncSrcSimple,					// DLL�ڲ�SrcSimple����

				&bEnableSOVITSPreResample,			// ����SOVITSģ�������Ƶ�ز���Ԥ����
				iSOVITSModelInputSamplerate,		// SOVITSģ����β�����
				& bEnableHUBERTPreResample,			// ����HUBERTģ�������Ƶ�ز���Ԥ����
				iHUBERTInputSampleRate,				// HUBERTģ����β�����
				
				&bDisableVolumeDetect,				// ռλ����ͣ��������⣨��������ģʽ��
				&bFoundJit,							// ռλ�����Ƿ������Jitter����
				fAvoidJitPrefixTime,				// Jitter�����ӵ�ǰ������������(s)
				&bDoItSignal						// ռλ������ʾ��worker�д����������
				).detach();

	//---always initialize the parent-------
	tresult result = AudioEffect::initialize (context);
	// if everything Ok, continue
	if (result != kResultOk)
	{
		return result;
	}

	//--- create Audio IO ------
	addAudioInput (STR16 ("Stereo In"), Steinberg::Vst::SpeakerArr::kStereo);
	addAudioOutput (STR16 ("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);

	/* If you don't need an event bus, you can remove the next line */
	addEventInput (STR16 ("Event In"), 1);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::terminate ()
{
	// Here the Plug-in will be de-instanciated, last possibility to remove some memory!
	
	//---do not forget to call parent ------
	return AudioEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::setActive (TBool state)
{
	//--- called when the Plug-in is enable/disable (On/Off) -----
	return AudioEffect::setActive (state);
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::process (Vst::ProcessData& data)
{
	//--- First : Read inputs parameter changes-----------
	char buff[100];
	
	// ��������仯
	if (data.inputParameterChanges)
	{
		int32 numParamsChanged = data.inputParameterChanges->getParameterCount();
		for (int32 index = 0; index < numParamsChanged; index++)
		{
			if (auto* paramQueue = data.inputParameterChanges->getParameterData(index))
			{
				Vst::ParamValue value;
				int32 sampleOffset;
				int32 numPoints = paramQueue->getPointCount();
				paramQueue->getPoint(numPoints - 1, sampleOffset, value);
				switch (paramQueue->getParameterId())
				{
				case kEnabelPitchErrorCalc:
					OutputDebugStringA("kEnabelPitchErrorCalc\n");
					bCalcPitchError = (bool)value;
					break;
				case kMaxSliceLength:
					OutputDebugStringA("kMaxSliceLength\n");
					fMaxSliceLength = value * maxMaxSliceLength + 0.1f;
					lMaxSliceLengthSampleNumber = this->processSetup.sampleRate * fMaxSliceLength;
					break;
				case kPitchChange:
					OutputDebugStringA("kPitchChange\n");
					fPitchChange = value * (maxPitchChange - minPitchChange) + minPitchChange;
					break;
				case kSelectRole:
					OutputDebugStringA("kSelectRole\n");
					iSelectRoleIndex = std::min<int>(
						(int)(roleList.size() * value), roleList.size() - 1);
					break;
				}
			}
		}
	}

	//--- Here you have to implement your processing
	Vst::Sample32* inputL = data.inputs[0].channelBuffers32[0];
	//Vst::Sample32* inputR = data.inputs[0].channelBuffers32[1];
	Vst::Sample32* outputL = data.outputs[0].channelBuffers32[0];
	Vst::Sample32* outputR = data.outputs[0].channelBuffers32[1];
	double fSampleMax = -9999;
	for (int32 i = 0; i < data.numSamples; i++) {
		// ��ȡ��ǰ����������
		double fCurrentSample = inputL[i];
		double fSampleAbs = std::abs(fCurrentSample);
		if (fSampleAbs > fSampleMax) {
			fSampleMax = fSampleAbs;
		}

		// ����ǰ�źŸ��Ƶ�ǰ���źŻ�������
		fPrefixBuffer[lPrefixBufferPos++] = fCurrentSample;
		if (lPrefixBufferPos == lPrefixLengthSampleNumber) {
			lPrefixBufferPos = 0;
		}
	}
	lPrefixBufferPos = (lPrefixBufferPos + data.numSamples) % lPrefixLengthSampleNumber;
	
	if (bDisableVolumeDetect) {
		// ���������������⣬������ֱ�Ӻϸ�
		bVolumeDetectFine = true;
	}
	else {
		bVolumeDetectFine = fSampleMax >= fSampleVolumeWorkActiveVal;
	}
	 
	if (bVolumeDetectFine) {
		fRecordIdleTime = 0.f;
	}
	else {
		fRecordIdleTime += 1.f * data.numSamples / this->processSetup.sampleRate;
		char buff[100];
		snprintf(buff, sizeof(buff), "��ǰ�ۻ�����ʱ��:%f\n", fRecordIdleTime);
		OutputDebugStringA(buff);
	}

	if (kRecordState == IDLE) {
		// ��ǰ�ǿ���״̬
		if (bVolumeDetectFine) {
			OutputDebugStringA("�л�������״̬");
			kRecordState = WORK;
			// ����ǰ����Ƶ����д�뵽ģ����λ�������
			for (int i = lPrefixBufferPos; i < lPrefixLengthSampleNumber; i++) {
				fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = fPrefixBuffer[i];
				if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lModelInputSampleBufferWritePos = 0;
				}
			}
			for (int i = 0; i < lPrefixBufferPos; i++) {
				fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = fPrefixBuffer[i];
				if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
					lModelInputSampleBufferWritePos = 0;
				}
			}
		}
	}
	else {
		// ��ǰ�ǹ���״̬
		// ����ǰ����Ƶ����д�뵽ģ����λ�������
		for (int i = 0; i < data.numSamples; i++) {
			fModeulInputSampleBuffer[lModelInputSampleBufferWritePos++] = inputL[i];
			if (lModelInputSampleBufferWritePos == lModelInputOutputBufferSize) {
				lModelInputSampleBufferWritePos = 0;
			}
		}

		// �ж��Ƿ���Ҫ�˳�����״̬
		bool bExitWorkState = false;

		// �˳�����1��������С�ҳ�������һ��ʱ��
		if (fRecordIdleTime >= fLowVolumeDetectTime) {
			bExitWorkState = true;
			OutputDebugStringA("������С�ҳ�������һ��ʱ�䣬ֱ�ӵ���ģ��\n");
		}

		// �˳�����2�����дﵽһ���Ĵ�С
		long inputBufferSize = func_cacl_read_write_buffer_data_size(lModelInputOutputBufferSize, lModelInputSampleBufferReadPos, lModelInputSampleBufferWritePos);
		if (inputBufferSize > lMaxSliceLengthSampleNumber + lPrefixLengthSampleNumber) {
			bExitWorkState = true;
			OutputDebugStringA("���д�С�ﵽԤ�ڣ�ֱ�ӵ���ģ��\n");
		}

		if (bExitWorkState) {
			// ��Ҫ�˳�����״̬
			kRecordState = IDLE;
			// worker��־λ����Ϊtrue����worker���
			bDoItSignal = true;
		}
	}

	// ���ģ������������������ݵĻ���д�뵽����ź���ȥ
	int channel = 0;
	bool bHasRightChanel = true;
	if (outputR == outputL || outputR == NULL) bHasRightChanel = false;
	snprintf(buff, sizeof(buff), "�����ָ��:%ld\n", lModelOutputSampleBufferReadPos);
	OutputDebugStringA(buff);
	if (lModelOutputSampleBufferReadPos != lModelOutputSampleBufferWritePos) {
		bFoundJit = false;
		bool bFinish = false;
		for (int i = 0; i < data.numSamples; i++)
		{
			bFinish = lModelOutputSampleBufferReadPos == lModelOutputSampleBufferWritePos;
			if (bFinish) {
				outputL[i] = 0.f;
				if (bHasRightChanel) {
					outputR[i] = 0.f;
				}
			}
			else {
				double currentSample = fModeulOutputSampleBuffer[lModelOutputSampleBufferReadPos++];
				if (lModelOutputSampleBufferReadPos == lModelInputOutputBufferSize) {
					lModelOutputSampleBufferReadPos = 0;
				}
				outputL[i] = currentSample;
				if (bHasRightChanel) {
					outputR[i] = currentSample;
				}
			}
		}
		if (bFinish) {
			// ����ȡ����
			OutputDebugStringA("����ȡ����\n");
		}
	}
	else {
		bFoundJit = true;
		lNoOutputCount += 1;
		char buff[100];
		snprintf(buff, sizeof(buff), "!!!!!!!!!!!!!!!!!!!!!!!��������:%ld\n", lNoOutputCount);
		std::string buffAsStdStr = buff;
		OutputDebugStringA(buff);
		for (int32 i = 0; i < data.numSamples; i++) {
			// ���������
			outputL[i] = 0.0000000001f;
			if (bHasRightChanel) {
				outputR[i] = 0.0000000001f;
			}
		}
	}
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::setupProcessing (Vst::ProcessSetup& newSetup)
{
	//--- called before any processing ----
	return AudioEffect::setupProcessing (newSetup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	// by default kSample32 is supported
	if (symbolicSampleSize == Vst::kSample32)
		return kResultTrue;

	// disable the following comment if your processing support kSample64
	/* if (symbolicSampleSize == Vst::kSample64)
		return kResultTrue; */

	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::setState (IBStream* state)
{
	// called when we load a preset, the model has to be reloaded
	IBStreamer streamer (state, kLittleEndian);
	bool bVal;
	float fVal;
	if (streamer.readFloat(fVal) == false) {
		return kResultFalse;
	}
	fMaxSliceLength = fVal * maxMaxSliceLength + 0.1f;
	lMaxSliceLengthSampleNumber = this->processSetup.sampleRate * fMaxSliceLength;
	if (streamer.readFloat(fVal) == false) {
		return kResultFalse;
	}
	fPitchChange = fVal * (maxPitchChange - minPitchChange) + minPitchChange;
	if (streamer.readBool(bVal) == false) {
		return kResultFalse;
	}
	bCalcPitchError = bVal;

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NetProcessProcessor::getState (IBStream* state)
{
	// here we need to save the model
	// �������õ��־û��ļ���
	IBStreamer streamer(state, kLittleEndian);

	streamer.writeFloat((fMaxSliceLength - 0.1f) / maxMaxSliceLength);
	streamer.writeFloat((fPitchChange - minPitchChange) / (maxPitchChange - minPitchChange));
	streamer.writeBool(bCalcPitchError);
	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace MyCompanyName

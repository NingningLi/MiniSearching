#include "MakeIndex.h"
#include "GetConfig.h"
#include "Rio.h"
#include "Tools.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

using namespace std;

MakeIndex::MakeIndex(GetConfig *config)
    :segment_(tools::trimEnter(config->getDict_path()).c_str(), 
              tools::trimEnter(config->getModel_path()).c_str()),
     libfile_(tools::trimEnter(config->getLibfile())),
     libindexfile_(tools::trimEnter(config->getLibIndexfile())),
     excludefile_(tools::trimEnter(config->getExcludefile())),
     indexfile_(tools::trimEnter(config->getIndexfile())),
     doctid_(1)
{
}

void MakeIndex::start()
{
    readExcludeFile(); //��һ���������ų����ĵ�
    readLibFile();  //�ڶ�����������ļ�
    countDoctFrequence();  //���Ĳ�������ĳ�������ڶ�����ҳ�г��ֹ���df
    calcWeight();  //���岽������ÿƪ��ҳ��ÿ�ε��ʵĴ�Ƶ
    normalization(); //��������Ȩ�ع�һ��
    saveIndex(); //���߲������浽�����ļ���
}

//�����ų����ĵ�
void MakeIndex::readExcludeFile()
{
    int fd_exclude = open(excludefile_.c_str(), O_RDONLY);
    if(fd_exclude == -1)
        throw runtime_error("open exclude file to read failed.");
    char buffer[1024] = {0};
    Rio rio_exclude(fd_exclude);
    while(rio_exclude.readLine(buffer, 1024) > 0){
        buffer[strlen(buffer) - 2] = 0;
        exclude_.insert(string(buffer));
    }
    exclude_.insert("\r");
    exclude_.insert("\n");
    cout << "Read exclude file over." << endl;
    close(fd_exclude);
}

//������ļ�
void MakeIndex::readLibFile()
{
    int fd_lib = open(libfile_.c_str(), O_RDONLY);
    if(fd_lib == -1)
        throw runtime_error("open lib file to read failed.");
    int fd_index = open(libindexfile_.c_str(), O_RDONLY);
    if(fd_index == -1)
        throw runtime_error("open lib index file to read failed.");

    Rio rio_lib(fd_lib);
    Rio rio_index(fd_index);

    char buffer[1024] = {0};
    size_t doctid;
    long offset;
    size_t len;
    while(rio_index.readLine(buffer, 1024) > 0){
        Document document;

        sscanf(buffer, "%u %ld %u", &doctid, &offset, &len);
        document.doctid_ = doctid;
        document.offset_ = offset;
        document.len_ = len;

        lseek(fd_lib, document.offset_, SEEK_SET);
        char contentbuffer[100000] = {0};
        read(fd_lib, contentbuffer, document.len_);

        string doct(contentbuffer);
        string::size_type pos1, pos2;

        //��ȡ��ҳ��url
        pos1 = doct.find("<url>") + 5;
        pos2 = doct.find("</url>");
        document.url_ = doct.substr(pos1, pos2 - pos1);

        //��ȡ��ҳ�ı���
        pos1 = doct.find("<title>") + 7;
        pos2 = doct.find("</title>");
        document.title_ = doct.substr(pos1, pos2 - pos1);

        //��ȡ��ҳ������
        pos1 = doct.find("<content>") + 9;
        pos2 = doct.find("</content>");
        document.content_ = doct.substr(pos1, pos2 - pos1);

        document_.push_back(document);
        doctid_ ++;
    }
    cout << "Read lib file over." << endl;
}

//ͳ��ÿһƪ��ҳ�Ĵ�Ƶ��tf
void MakeIndex::countDoctFrequence()
{
    list<Document>::iterator listit;
    for(listit = document_.begin(); listit != document_.end(); ++listit){
        //cout << "Count one page over."  << endl;
        //��һ����ȥ��Ӣ�ĵı����ţ���������ĸ��д�ĵ���ת��ΪСд
        string content = listit->content_;
        tools::deleteEnPunct(content);

        //�ڶ������ִ�
        vector<string>words;
        segment_.cut(content, words);

        //��������ͳ�ƴ�Ƶ
        vector<string>::iterator it;
        unordered_set<string>::iterator setit;
        map<string, int> wordfre;
        for(it = words.begin(); it != words.end(); ++it){
            setit = exclude_.find(*it);
            if(setit == exclude_.end())
                df_[*it][listit->doctid_]++;
        }
    }
    cout << "TF and DF is count over." << endl;
}

//����ÿһ�����ʵ�Ȩ��
void MakeIndex::calcWeight()
{
    //��һ����Ҫ�Ǳ���ÿƪ�����е�ÿһ�����ʣ���������Ȩ��
    map<string, map<size_t, int> >::iterator dfit;
    map<size_t, int>::iterator tfit;
    //����Ȩ�أ������±�ţ����ʣ�Ȩ�ز���Ȩ�����ݽṹ��
    for(dfit = df_.begin(); dfit != df_.end(); ++dfit)
        for(tfit = dfit->second.begin(); tfit != dfit->second.end(); ++tfit)
            weight_[dfit->first][tfit->first] = tfit->second * log(doctid_ / dfit->second.size());
    cout << "Weight is count over." << endl;
}

//Ȩ�ع�һ������
void MakeIndex::normalization()
{
    /*
    list<Document>::iterator listit; 
    map<string, map<size_t, double> >::iterator mapit;
    map<size_t, double>::iterator sizeit;
    for(listit = document_.begin(); listit != document_.end(); ++listit){
        double denominator = 0;
        //��һ��ѭ�����ҳ���Ӧĳһƪ��ҳ�����е���Ȩ�أ��������ǵ�ƽ����
        for(mapit = weight_.begin(); mapit != weight_.end(); ++mapit)
            if((sizeit = mapit->second.find(listit->doctid_)) != mapit->second.end())
                denominator += sizeit->second * sizeit->second;

        //�Է�ĸ����
        denominator = sqrt(denominator);

        cout << listit->doctid_ << endl;
        //�ڶ���ѭ�����ø�ƪ��ҳ�����е���Ȩ�س��������������ķ�ĸ����Ȩ�ع�һ��
        for(mapit = weight_.begin(); mapit != weight_.end(); ++mapit)
            if((sizeit = mapit->second.find(listit->doctid_)) != mapit->second.end())
                sizeit->second /= denominator;
    }
    */

    //Ȩֵ��һ������Ҳ����ʹ������Ĵ��룬����ĸ������׶���ֻ�Ƕ�ķ���һ�����ڴ�
    //���ݽṹ������������
    map<size_t, map<string, double> > normalize;
    map<size_t, map<string, double> >::iterator doctit; 
    map<string, map<size_t, double> >::iterator wordit;
    map<size_t, double>::iterator sizeit;
    map<string, double>::iterator strit;

    //��һ������map<string, map<size_t, double> >���͵����ݽṹ
    //    ת��Ϊmap<size_t, map<string, double> >���͵����ݽṹ
    for(wordit = weight_.begin(); wordit != weight_.end(); ++wordit)
         for(sizeit = wordit->second.begin(); sizeit != wordit->second.end(); ++sizeit)
             normalize[sizeit->first].insert(make_pair(wordit->first, sizeit->second));

    //�ڶ���������Ȩ�ع�һ������
    for(doctit = normalize.begin(); doctit != normalize.end(); ++doctit){
        double denominator = 0;
        for(strit = doctit->second.begin(); strit != doctit->second.end(); ++strit)
            denominator += strit->second * strit->second;
        denominator = sqrt(denominator);
        for(strit = doctit->second.begin(); strit != doctit->second.end(); ++strit)
            strit->second /= denominator;
        cout << doctit->first << endl;
    }
    //���������ù�һ�����Ȩ��ֵ����֮ǰ��Ȩ��ֵ
    for(doctit = normalize.begin(); doctit != normalize.end(); ++doctit)
        for(strit = doctit->second.begin(); strit != doctit->second.end(); ++strit)
            weight_[strit->first][doctit->first] = strit->second;
    cout << "Normalization is over." << endl;
}

//����������Ϣ�������ļ���
void MakeIndex::saveIndex()
{
    int index_write;
    if(-1 == (index_write = open(indexfile_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666)))
        throw runtime_error("Open index file to write fail.");

    Rio rio_write(index_write);

    map<string, map<size_t, double> >::iterator wordit;
    map<size_t, double>::iterator sizeit;
    for(wordit = weight_.begin(); wordit != weight_.end(); ++wordit){
        ostringstream oss;
        oss << wordit->first;

        //��һ������ʽ��������Ϣ
        for(sizeit = wordit->second.begin(); sizeit != wordit->second.end(); ++sizeit)
             oss << " " << sizeit->first << " " << sizeit->second;
        oss << endl;

        //�ڶ�����д�������ļ�
        string indexstr = oss.str();
        rio_write.writen(indexstr.c_str(), indexstr.size()); 
    }
    close(index_write);
}


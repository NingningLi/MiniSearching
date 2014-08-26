#include "Query.h"
#include "Rio.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <queue>
using namespace std;

Query::Query(GetConfig *config)
    :segment_(tools::trimEnter(config->getDict_path()).c_str(), 
              tools::trimEnter(config->getModel_path()).c_str()),
     excludefile_(tools::trimEnter(config->getExcludefile().c_str())),
     indexfile_(tools::trimEnter(config->getIndexfile())),
     libfile_(tools::trimEnter(config->getLibfile())),
     libindexfile_(tools::trimEnter(config->getLibIndexfile())),
     doctid_(0)
{
    readExcludeFile();
    readLibIndexFile();
    readIndexFile(); 
}

void Query::readExcludeFile()
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

void Query::readLibIndexFile()
{
    int fd_index = open(libindexfile_.c_str(), O_RDONLY);
    if(fd_index == -1)
        throw runtime_error("open lib index file to read failed.");

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

        libindex_[doctid] = document;
        doctid_ ++;
    }
    close(fd_index);
    cout << "Read lib index file over." << endl;
}

void Query::readIndexFile()
{
    //���������е����ݰ������ʡ���ҳ��š���һ��Ȩ�ء������ĸ�ʽ������
    FILE *fp_index = fopen(indexfile_.c_str(), "r");
    if(fp_index == NULL)
        throw runtime_error("open index file to read fail.");

    char buffer[100000];
    string param1;
    string param2;
    string word;
    int doctid;
    double weight;
    while(fgets(buffer, 100000, fp_index) != NULL){
        istringstream oss(buffer);
        oss >> word;
        while(oss >> doctid >> weight)
            weight_word_[word].insert(make_pair(doctid, weight));
    }
    fclose(fp_index);
    cout << "Read index file over." << endl;
}

void Query::query(string str)
{
    map<string, double> queryword;
    //��������ַ����������������һ����Ȩֵ
    if(!queryWordNormaraize(str, queryword)){
        cout << "Sorry, we haven't found!" << endl;
        return;
    }
    
    //�����ѯ������df��С�ĵ���
    vector<size_t> df_count;
    map<string, double>::iterator witit;
    for(witit = queryword.begin(); witit != queryword.end(); ++witit){
        df_count.push_back(weight_word_[witit->first].size()); 
    }
    vector<size_t>::iterator svec;
    size_t mindf = df_count.front();
    map<string, double>::iterator witpreit, witcurit;
    witpreit = witcurit = queryword.begin();
    for(svec = df_count.begin(); svec != df_count.end(); ++svec, ++witcurit){
        if(*svec < mindf){
            mindf = *svec;
            witpreit = witcurit;
        }
    }
    //��df��С���Ǹ����ʳ��ֵ���ҳ�б�
    set<size_t>docids;
    map<size_t, double>::iterator sizeit;
    map<size_t, double> &docidlist = weight_word_[witpreit->first];
    for(sizeit = docidlist.begin(); sizeit != docidlist.end(); ++ sizeit)
        docids.insert(sizeit->first);

    //�����е��ʶ�Ӧ����ҳ�б�Ľ���
    set<size_t>::iterator setit; //���������ҳ���������ݽṹ
    map<size_t, double>::iterator rtn;
    string firstword = witpreit->first;
    for(witit = queryword.begin(); witit != queryword.end(); ++witit){

        //��һ�����ж��Ƿ���df��С���Ǹ����ʣ�����ǣ�������
        if(witit->first == firstword)
            continue;

        //�ڶ�����������ҳ�б���ÿ����ҳ�в���Ҫ��ѯ�ĵ��ʣ�
        //���ĳ����ҳû�ҵ�����˵������ҳ������Ҫ����ҳ��ɾ֮~
        map<size_t, double> &temp = weight_word_[witit->first];
        for(setit = docids.begin(); setit != docids.end(); ++setit){
            if(temp.find(*setit) == temp.end()){
                docids.erase(setit);
                setit --;
            }

            //˵�����е��������һ���ʱ����û�к��ʵĽ���ġ�
            if(docids.size() == 0){
                cout << "Sorry, we haven't found!" << endl;
                return;
            }
        }
    } 

    //��������õķ�����������ҳ���������Ͳ�ѯ�ı�����������˲�����
    //�������������ȼ������У����ֻ���ͷ��β�ĴӶ�����ȡ������
    priority_queue<PageSimilarity> queue;
    cout << "Total found about " << docids.size() << " pages." << endl;
    //��ͷ��β�����ĵ��������������ƶȣ��������ȼ�����
    for(setit = docids.begin(); setit != docids.end(); ++setit){
        double similarity = 0;
        for(witit = queryword.begin(); witit != queryword.end(); ++witit){
            similarity += witit->second * weight_word_[witit->first][*setit];
        }
        PageSimilarity newpage = {*setit, similarity};
        queue.push(newpage);
    }
    //�����ղ�ѯ�õ���ҳ�б���ǰ̨չʾ
    while(!queue.empty()){
        display(queue.top().doctid_);
        queue.pop();
    }
}

int Query::queryWordNormaraize(string str, map<string, double> &queryword)
{
    //������Ĳ�ѯ�ַ����д�
    vector<string> words;
    segment_.cut(str, words);

    //ȥ��ͣ�ôʲ�ͳ�ƴ�Ƶ
    tools::deleteEnPunct(str);
    map<string, int> querywordfrequence; //������¼�����ַ�����ÿ�������Ƶ��
    vector<string>::iterator ivec;
    for(ivec = words.begin(); ivec != words.end(); ++ ivec){
        if(exclude_.find(*ivec) == exclude_.end())
            querywordfrequence[*ivec] ++;
    }
    
    cout << querywordfrequence.size() << endl;
    for(map<string, int>::iterator it = querywordfrequence.begin(); it != querywordfrequence.end(); ++it)
        cout << it->first << endl;

    //����Ȩֵ
    for(ivec = words.begin(); ivec != words.end(); ++ ivec){
        double denominator = weight_word_[*ivec].size();
        //�����������ҳ���в�����ʱ�����ص�ֵ���㣬��Ҫ�Ѹô���֮
        //���û�������ַ�����û��һ�������Ǵʿ����еģ��ͷ��ز�ѯ���Ϊ��
        if(denominator == 0){
            words.erase(ivec);
            --ivec;
            if(words.size() == 0)
                return 0;
            continue;
        }
        //�ôʴ���ʱ��������Ȩֵ
        queryword[*ivec] = querywordfrequence[*ivec] * log(doctid_ / denominator);
    }
    
    //��Ȩֵ��һ���ķ�ĸ
    double denominator;
    map<string, double>::iterator witit;
    for(witit = queryword.begin(); witit != queryword.end(); ++ witit)
        denominator += witit->second * witit->second;
    denominator = sqrt(denominator);

    //��ѯ�ı���Ȩֵ��һ��
    for(witit = queryword.begin(); witit != queryword.end(); ++ witit)
        witit->second /= denominator;
    return 1;
}

void Query::display(size_t doctid)
{
    Document doct = libindex_[doctid];
    int fd_lib = open(libfile_.c_str(), O_RDONLY);
    if(fd_lib == -1)
        throw runtime_error("open lib file to read failed.");
    
    lseek(fd_lib, doct.offset_, SEEK_SET);
    char buffer[100000] = {0};
    read(fd_lib, buffer, doct.len_);
    cout << buffer << endl;
    close(fd_lib);
}

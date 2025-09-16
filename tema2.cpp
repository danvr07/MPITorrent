#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <algorithm>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 10
#define HASH_SIZE 32
#define MAX_CHUNKS 100

#define INITIALIZE_TAG 0
#define INFO_TAG 1
#define COMMUNICATION_TAG 5
#define UPLOAD_TAG 6
#define DOWLOAD_HASH_TAG 100

using namespace std;

// Structuri   de date
typedef struct
{
    char filename[MAX_FILENAME];
    int num_segments;
    bool isSeed;
    vector<string> segment_hashes;
} FileDetails;

typedef struct
{
    char filename[MAX_FILENAME]; // Numele fisierului
    vector<int> peers;           // Lista de peers care detin acest fisier(Swarm)
} SwarmInfo;

enum RequestType
{
    REQUEST_FINISH,
    REQUEST_SWARM,
    REQUEST_UNKNOWN
};

vector<FileDetails> trackerFileInfo;
vector<string> filesWanted;
vector<FileDetails> file_infoGlobal;
int nrFiles;

// afiseaza informatiile din trackerFileInfo
void printTrackerFileInfo(const vector<FileDetails> &trackerFileInfo)
{
    cout << "Tracker File Info:" << endl;
    for (size_t i = 0; i < trackerFileInfo.size(); i++)
    {
        const FileDetails &fileInfo = trackerFileInfo[i];
        cout << "Fisier " << i + 1 << ":" << endl;
        cout << "  Nume: " << fileInfo.filename << endl;
        cout << "  Numar de segmente: " << fileInfo.num_segments << endl;
        cout << "  Hash-urile segmentelor:" << endl;
        for (size_t j = 0; j < fileInfo.segment_hashes.size(); j++)
        {
            cout << "    Segment " << j + 1 << ": " << fileInfo.segment_hashes[j] << endl;
        }
    }
}

RequestType getRequestType(const char *filenameWant)
{
    if (strcmp(filenameWant, "FINISH") == 0)
    {
        return REQUEST_FINISH;
    }
    return REQUEST_SWARM;
}

// afiseaza informatiile din downloadedTrackerFileInfo
void printDownloadedTrackerFileInfo(const vector<FileDetails> &downloadedTrackerFileInfo)
{
    cout << "Downloaded Tracker File Info:" << endl;
    for (size_t i = 0; i < downloadedTrackerFileInfo.size(); i++)
    {
        const FileDetails &fileInfo = downloadedTrackerFileInfo[i];
        cout << "Fisier " << i + 1 << ":" << endl;
        cout << "  Nume: " << fileInfo.filename << endl;
        cout << "  Numar de segmente: " << fileInfo.num_segments << endl;
        cout << "  Hash-urile segmentelor:" << endl;
        for (size_t j = 0; j < fileInfo.segment_hashes.size(); j++)
        {
            cout << "    Segment " << j + 1 << ": " << fileInfo.segment_hashes[j] << endl;
        }
    }
}

// afiseaza informatiile din file_infoGlobal
void printData(const vector<FileDetails> &file_infoGlobal, int rank)
{
    cout << "===== File DATA [" << rank << "] =====" << endl;
    for (const auto &file : file_infoGlobal)
    {
        cout << "Fisier: " << file.filename << endl;
        cout << "Numar segmente: " << file.num_segments << endl;
        cout << "Hash-uri segmente:" << endl;

        for (size_t j = 0; j < file.segment_hashes.size(); j++)
        {
            cout << "  Segment " << j + 1 << ": " << file.segment_hashes[j] << endl;
        }

        cout << endl;
    }
}

// verifica daca hash ul exista in file_infoGlobal
int verifyHashInFile(vector<FileDetails> file_infoGlobal, int nrFiles, const string &fileWanted, const string &hash, int rank)
{
    // Cautam fisierul specificat in file_infoGlobal
    for (int i = 0; i < nrFiles; i++)
    {
        if (fileWanted == file_infoGlobal[i].filename)
        {
            // Cautam hash-ul in hash-urile segmentelor fisierului dorit
            for (const string &localHash : file_infoGlobal[i].segment_hashes)
            {
                if (hash == localHash)
                {
                    // Returnam 1 daca hash-ul este gasit
                    return 1;
                }
            }

            return 0;
        }
    }

    return 0;
}

int receivePeersSize()
{
    int peersSize;
    MPI_Recv(&peersSize, 1, MPI_INT, TRACKER_RANK, COMMUNICATION_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    return peersSize;
}

vector<int> receivePeersVector(int peersSize)
{
    vector<int> peers(peersSize);
    MPI_Recv(&peers[0], peersSize, MPI_INT, TRACKER_RANK, COMMUNICATION_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    return peers;
}

// salveaza fisierul fileWanted
void saveDownloadedFile(int rank, const string &fileWanted, const vector<string> &segment_hashes, int numTotal)
{
    // Construim numele fișierului
    string fileClient = "client" + to_string(rank) + "_" + fileWanted;

    // Construim conținutul fișierului într-un buffer
    ostringstream buffer;
    for (const string &hash : segment_hashes)
    {
        buffer << hash << "\n";
    }

    // Scriem buffer-ul în fișier
    ofstream out(fileClient);
    if (out.is_open())
    {
        out << buffer.str();
        out.close();
    }
    else
    {
        cout << "Eroare la deschiderea fisierului " << fileClient << endl;
    }
}

// adauga hash ul la file_infoGlobal pentru filename
void addSegmentHash(vector<FileDetails> &file_infoGlobal, const string &filename, const string &hash)
{
    for (auto &fileInfo : file_infoGlobal)
    {
        if (fileInfo.filename != filename)
        {
            continue; // Sari peste fișierele care nu corespund
        }

        // Verifică manual dacă hash-ul există deja
        bool hashExists = false;
        for (auto it = fileInfo.segment_hashes.begin(); it != fileInfo.segment_hashes.end(); ++it)
        {
            if (*it == hash)
            {
                hashExists = true;
                break; // Hash-ul există deja, deci ieșim din buclă
            }
        }

        // Dacă hash-ul nu există, îl adăugăm
        if (!hashExists)
        {
            fileInfo.segment_hashes.push_back(hash);
            fileInfo.num_segments = fileInfo.segment_hashes.size();
        }
        return; // După actualizarea unui fișier, ieșim din funcție
    }
}

void *download_thread_func(void *arg)
{
    int rank = *(int *)arg;

    // cerem datele de la tracker
    vector<FileDetails> downloadedTrackerFileInfo;

    int trackerFileInfoSize;
    MPI_Recv(&trackerFileInfoSize, 1, MPI_INT, TRACKER_RANK, INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    for (int i = 0; i < trackerFileInfoSize; i++)
    {
        FileDetails fileInfo;
        // Primeste numele fisierului
        MPI_Recv(fileInfo.filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        // Primeste numarul de segmente
        MPI_Recv(&fileInfo.num_segments, 1, MPI_INT, TRACKER_RANK, INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        // Redimensioneaza vectorul pentru a primi hash-urile segmentelor
        fileInfo.segment_hashes.resize(fileInfo.num_segments);

        for (int k = 0; k < fileInfo.num_segments; k++)
        {
            char hash[HASH_SIZE];
            MPI_Recv(hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            fileInfo.segment_hashes[k] = string(hash, HASH_SIZE);
        }

        // adauga structura primita in vectorul local
        downloadedTrackerFileInfo.push_back(fileInfo);
    }

    // printDownloadedTrackerFileInfo(downloadedTrackerFileInfo);

    for (const string &fileWanted : filesWanted)
    {
        // cerem numarul total de segmente si hash urile fisierului dorit
        int numTotal = 0;
        vector<string> segment_hashes;
        vector<string> hashes;

        for (const FileDetails &fileInfo : downloadedTrackerFileInfo)
        {
            if (fileWanted == fileInfo.filename)
            {
                numTotal = fileInfo.num_segments;
                segment_hashes = fileInfo.segment_hashes;
                break;
            }
        }

        // cerem swarm ul de la tracker pentru fisierul fileWanted
        MPI_Send(fileWanted.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, COMMUNICATION_TAG, MPI_COMM_WORLD);
        int peersSize = receivePeersSize();
        vector<int> receivedPeers = receivePeersVector(peersSize);
        int segmenteDescarcate = 0;

        for (int i = 0; i < numTotal; i++)
        {
            // Verificam daca avem deja hash-ul
            if (verifyHashInFile(file_infoGlobal, nrFiles, fileWanted, segment_hashes[i], rank))
            {
                hashes.push_back(segment_hashes[i]);
                continue;
            }

            // Daca nu avem hash-ul, trimitem cerere catre un peer din swarm
            int peerIndex = i % receivedPeers.size();
            int peer = receivedPeers[peerIndex];
            bool hashReceived = false;

            while (!hashReceived && !receivedPeers.empty())
            {
                // Daca peer-ul are acelasi rank cu procesul curent, trecem la urmatorul
                if (peer == rank)
                {
                    peerIndex = (peerIndex + 1) % receivedPeers.size();
                    peer = receivedPeers[peerIndex];
                    continue;
                }

                // Selectam un peer din swarm
                // printf("[%d] Solicit hash ul %s din fisierul %s de la peer ul %d\n", rank, segment_hashes[i].c_str(), fileWanted.c_str(), peer);
                // Trimitem cererea de descarcare
                MPI_Send(segment_hashes[i].c_str(), HASH_SIZE, MPI_CHAR, peer, UPLOAD_TAG, MPI_COMM_WORLD);

                // Primesc mesajul de confirmare de la peer
                char message[HASH_SIZE];
                MPI_Recv(message, HASH_SIZE, MPI_CHAR, peer, DOWLOAD_HASH_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                if (strcmp(message, "YES") == 0)
                {
                    // Hash-ul a fost primit
                    hashes.push_back(segment_hashes[i]);
                    addSegmentHash(file_infoGlobal, fileWanted, segment_hashes[i]);
                    segmenteDescarcate++;
                    hashReceived = true;
                }
                else
                {
                    // Daca peer-ul nu are hash-ul, trecem la urmatorul peer
                    peerIndex = (peerIndex + 1) % receivedPeers.size();
                    peer = receivedPeers[peerIndex];
                }

                // Cerem actualizarea swarm-ului la fiecare 10 segmente descarcate
                if (segmenteDescarcate % 10 == 0 && segmenteDescarcate > 0)
                {
                    MPI_Send(fileWanted.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, COMMUNICATION_TAG, MPI_COMM_WORLD);

                    int localPeersSize;
                    MPI_Recv(&localPeersSize, 1, MPI_INT, TRACKER_RANK, COMMUNICATION_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                    vector<int> localReceivedPeers(localPeersSize);
                    MPI_Recv(&localReceivedPeers[0], localPeersSize, MPI_INT, TRACKER_RANK, COMMUNICATION_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                    receivedPeers = localReceivedPeers;
                }
            }
        }
        // Salveaza fisierul descarcat
        saveDownloadedFile(rank, fileWanted, hashes, numTotal);
    }

    MPI_Send("FINISH", MAX_FILENAME, MPI_CHAR, TRACKER_RANK, COMMUNICATION_TAG, MPI_COMM_WORLD);

    return NULL;
}

bool hasHashInAnyFile(const vector<FileDetails> &file_infoGlobal, const string &targetHash)
{
    // Iteram prin toate fisierele din vector
    for (const auto &fileInfo : file_infoGlobal)
    {
        // Iteram prin toate hash-urile segmentelor fisierului curent
        for (const auto &hash : fileInfo.segment_hashes)
        {
            // Verificam daca hash-ul curent este egal cu targetHash
            if (hash == targetHash)
            {
                return true; // Hash-ul a fost gasit
            }
        }
    }
    return false; // Hash-ul nu a fost gasit in niciun fisier
}

void *upload_thread_func(void *arg)
{
    while (1)
    {
        MPI_Status status;
        char targetHash[HASH_SIZE];
        MPI_Recv(targetHash, HASH_SIZE, MPI_CHAR, MPI_ANY_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD, &status);

        if (strcmp(targetHash, "END") == 0)
        {
            // daca primeste END, se opreste
            break;
        }
        else
        {
            // verifica daca are hash ul sau nu
            if (hasHashInAnyFile(file_infoGlobal, targetHash))
            {
                MPI_Send("YES", HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, DOWLOAD_HASH_TAG, MPI_COMM_WORLD);
            }
            else
            {
                MPI_Send("NO", HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, DOWLOAD_HASH_TAG, MPI_COMM_WORLD);
            }
        }
    }

    return NULL;
}

void updateTrackerFileInfo(vector<FileDetails> &trackerFileInfo, const FileDetails &newFileInfo);
void updateSwarmInfo(const FileDetails &fileInfo, int peer_rank, vector<SwarmInfo> &swarmInfoList);

void sendDataToTracker(int nrFiles, const vector<FileDetails> &file_infoGlobal, int tracker_rank)
{
    // Trimitem numărul de fișiere către tracker
    MPI_Send(&nrFiles, 1, MPI_INT, tracker_rank, INITIALIZE_TAG, MPI_COMM_WORLD);

    // Trimitem trackerului numele fișierelor, numărul de segmente și hash-urile segmentelor
    for (int i = 0; i < nrFiles; i++)
    {
        MPI_Send(file_infoGlobal[i].filename, MAX_FILENAME, MPI_CHAR, tracker_rank, INITIALIZE_TAG, MPI_COMM_WORLD);
        MPI_Send(&file_infoGlobal[i].num_segments, 1, MPI_INT, tracker_rank, INITIALIZE_TAG, MPI_COMM_WORLD);

        for (size_t j = 0; j < file_infoGlobal[i].segment_hashes.size(); j++)
        {
            MPI_Send(file_infoGlobal[i].segment_hashes[j].c_str(), HASH_SIZE, MPI_CHAR, tracker_rank, INITIALIZE_TAG, MPI_COMM_WORLD);
        }
    }
}

void receiveDataFromPeers(int peer_rank, vector<FileDetails> &trackerFileInfo, vector<SwarmInfo> &swarmInfoList)
{
    MPI_Status status;
    int nrFiles;

    // Primeste numarul de fisiere de la peer
    MPI_Recv(&nrFiles, 1, MPI_INT, peer_rank, INITIALIZE_TAG, MPI_COMM_WORLD, &status);

    for (int j = 0; j < nrFiles; j++)
    {
        FileDetails fileInfo;

        // Primeste numele fisierului
        MPI_Recv(fileInfo.filename, MAX_FILENAME, MPI_CHAR, peer_rank, INITIALIZE_TAG, MPI_COMM_WORLD, &status);

        // Primeste numarul de segmente
        MPI_Recv(&fileInfo.num_segments, 1, MPI_INT, peer_rank, INITIALIZE_TAG, MPI_COMM_WORLD, &status);

        // Redimensioneaza vectorul pentru a acomoda hash-urile segmentelor
        fileInfo.segment_hashes.resize(fileInfo.num_segments);

        for (int k = 0; k < fileInfo.num_segments; k++)
        {
            // Primeste hash-ul fiecarui segment
            char hash[HASH_SIZE];
            MPI_Recv(hash, HASH_SIZE, MPI_CHAR, peer_rank, INITIALIZE_TAG, MPI_COMM_WORLD, &status);

            // Convertim hash-ul primit intr-un string si il salvam
            fileInfo.segment_hashes[k] = string(hash, HASH_SIZE);
        }

        // Salveaza informatiile fisierului in vectorul `trackerFileInfo`
        updateTrackerFileInfo(trackerFileInfo, fileInfo);

        // Actualizeaza swarmInfoList cu informatiile primite
        updateSwarmInfo(fileInfo, peer_rank, swarmInfoList);
    }

    // Trimite un ACK peer-ului
}

// actualizeaza informatiile din trackerFileInfo
void updateTrackerFileInfo(vector<FileDetails> &trackerFileInfo, const FileDetails &newFileInfo)
{
    // Iterăm prin trackerFileInfo folosind un iterator
    for (auto it = trackerFileInfo.begin(); it != trackerFileInfo.end(); ++it)
    {
        if (strcmp(it->filename, newFileInfo.filename) == 0)
        {
            // Iterăm prin noile hash-uri
            for (const auto &newHash : newFileInfo.segment_hashes)
            {
                // Verificăm manual dacă hash-ul există deja
                bool hashExists = false;
                for (auto hashIt = it->segment_hashes.begin(); hashIt != it->segment_hashes.end(); ++hashIt)
                {
                    if (*hashIt == newHash)
                    {
                        hashExists = true;
                        break; // Hash-ul există deja, nu mai continuăm căutarea
                    }
                }

                // Dacă hash-ul nu există, îl adăugăm
                if (!hashExists)
                {
                    it->segment_hashes.push_back(newHash);
                }
            }

            it->isSeed = true; // Marcăm fișierul ca Seed
            return;            // Ieșim din funcție, deoarece fișierul a fost găsit și actualizat
        }
    }

    // Dacă fișierul nu există, adăugăm o nouă intrare
    trackerFileInfo.push_back(newFileInfo);
}
void addPeerIfNotExists(vector<int> &peers, int peer)
{
    if (find(peers.begin(), peers.end(), peer) == peers.end())
    {
        peers.push_back(peer);
    }
}

// actualizeaza datele din swarmInfoList
void updateSwarmInfo(const FileDetails &fileInfo, int peer_rank, vector<SwarmInfo> &swarmInfoList)
{
    bool fileFound = false;

    // Verificam daca fisierul exista deja in swarmInfoList
    for (auto &swarm : swarmInfoList)
    {
        if (strcmp(swarm.filename, fileInfo.filename) == 0)
        {
            // Daca fisierul exista, adaugam peer-ul la lista de peers daca nu exista deja
            addPeerIfNotExists(swarm.peers, peer_rank);
            fileFound = true;
            break;
        }
    }

    // Daca fisierul nu exista, adaugam un nou swarm pentru fisier
    if (!fileFound)
    {
        SwarmInfo newSwarm;
        strncpy(newSwarm.filename, fileInfo.filename, MAX_FILENAME);
        newSwarm.peers.push_back(peer_rank);
        swarmInfoList.push_back(newSwarm);
    }
}
void tracker(int numtasks, int rank)
{
    // Structura pentru a stoca informatiile primite de la peers
    vector<SwarmInfo> swarmInfoList;
    int exitCount = numtasks - 1;

    for (int i = 1; i < numtasks; i++)
    {
        receiveDataFromPeers(i, trackerFileInfo, swarmInfoList);
        char ack[] = "ACK";
        MPI_Send(ack, 4, MPI_CHAR, i, INITIALIZE_TAG, MPI_COMM_WORLD);
    }

    // printTrackerFileInfo(trackerFileInfo); print pentru debug

    // trimite datele catre fiecare client
    for (int i = 1; i < numtasks; i++)
    {
        // trimite trackerfileinfo.size
        int trackerFileInfoSize = trackerFileInfo.size();
        MPI_Send(&trackerFileInfoSize, 1, MPI_INT, i, INFO_TAG, MPI_COMM_WORLD);
        // trimite tot trackerfileinfo
        for (size_t j = 0; j < trackerFileInfo.size(); j++)
        {
            MPI_Send(trackerFileInfo[j].filename, MAX_FILENAME, MPI_CHAR, i, INFO_TAG, MPI_COMM_WORLD);
            MPI_Send(&trackerFileInfo[j].num_segments, 1, MPI_INT, i, INFO_TAG, MPI_COMM_WORLD);

            for (int k = 0; k < trackerFileInfo[j].num_segments; k++)
            {
                MPI_Send(trackerFileInfo[j].segment_hashes[k].c_str(), HASH_SIZE, MPI_CHAR, i, INFO_TAG, MPI_COMM_WORLD);
            }
        }
    }

    while (1)
    {
        MPI_Status status;
        char filenameWant[MAX_FILENAME];
        MPI_Recv(filenameWant, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, COMMUNICATION_TAG, MPI_COMM_WORLD, &status);
        RequestType requestType = getRequestType(filenameWant);

        switch (requestType)
        {
        case REQUEST_FINISH:
            // Numărăm clienții care au terminat descărcarea
            exitCount--;
            if (exitCount == 0)
            {
                // Trimitem END fiecărui thread de upload
                for (int i = 1; i < numtasks; i++)
                {
                    MPI_Send("END", HASH_SIZE, MPI_CHAR, i, UPLOAD_TAG, MPI_COMM_WORLD);
                }
                break;
            }
            break;

        case REQUEST_SWARM:
            // Răspundem la cererea de swarm
            for (auto &swarmInfo : swarmInfoList)
            {
                if (strcmp(swarmInfo.filename, filenameWant) == 0)
                {
                    // Trimitem dimensiunea vectorului de peers
                    int peersSize = swarmInfo.peers.size();
                    MPI_Send(&peersSize, 1, MPI_INT, status.MPI_SOURCE, COMMUNICATION_TAG, MPI_COMM_WORLD);

                    // Trimitem lista de peers
                    MPI_Send(&swarmInfo.peers[0], peersSize, MPI_INT, status.MPI_SOURCE, COMMUNICATION_TAG, MPI_COMM_WORLD);

                    // Verificăm dacă clientul nu este deja în swarm
                    addPeerIfNotExists(swarmInfo.peers, status.MPI_SOURCE);
                    break;
                }
            }
            break;

        case REQUEST_UNKNOWN:
        default:
            cout << "Cerere necunoscută primită: " << endl;
            break;
        }

        // Verificăm dacă trebuie să ieșim din bucla principală
        if (exitCount == 0)
        {
            break;
        }
    }
}

void peer(int numtasks, int rank)
{
    // Construirea numelui fisierului
    ostringstream filename;
    filename << "in" << rank << ".txt";
    string filename_str = filename.str();

    // Deschidere fisier folosind ifstream
    ifstream file(filename_str);
    if (!file)
    {
        cerr << "Eroare la citire" << endl;
        exit(1);
    }

    file >> nrFiles;

    // salvam fisierele detinute
    for (int i = 0; i < nrFiles; i++)
    {
        FileDetails fileInfo;
        file >> fileInfo.filename >> fileInfo.num_segments;

        // Alocam spatiu pentru segment_hashes
        fileInfo.segment_hashes.resize(fileInfo.num_segments);

        for (int j = 0; j < fileInfo.num_segments; j++)
        {
            file >> fileInfo.segment_hashes[j];
        }

        // Adauga structura completa in vectorul file_infoGlobal
        file_infoGlobal.push_back(fileInfo);
    }

    // printData(file_infoGlobal, rank); // print pentru debug

    int nedeed_files = 0;
    file >> nedeed_files;
    for (int i = 0; i < nedeed_files; i++)
    {
        string filename;
        file >> filename;
        filesWanted.push_back(filename);
    }

    file.close();

    // trimite datele catre tracker
    sendDataToTracker(nrFiles, file_infoGlobal, TRACKER_RANK);

    // primeste confirmare de la tracker
    char ack[4];
    MPI_Recv(ack, 4, MPI_CHAR, TRACKER_RANK, INITIALIZE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *)&rank);
    if (r)
    {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)&rank);
    if (r)
    {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r)
    {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r)
    {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}

int main(int argc, char *argv[])
{
    int numtasks, rank;

    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE)
    {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK)
    {
        tracker(numtasks, rank);
    }
    else
    {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
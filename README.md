Acest proiect implementeaza un protocol simplificat de tip BitTorrent,
utilizand MPI si Pthreads pentru distributia
fisierelor intre mai multi clienti si un tracker.

In aceasta tema mai mult se simuleaza procesul de descarcare a unor fisiere
intre mai multe noduri dintr-o retea distribuita, fara a implementa complet
toate caracteristicile unui client BitTorrent real.
	1.	In functia peer citesc informatiile de la clienti nr de fisiere si segmentele sale,
precum si numele fisierlor pe care doreste sa le descarce, apoi aceste date sunt trimise catre tracker,
odata ce tracker ul primeste informatie de la fiecare client, trimite un mesaj ACK.
Datele sunt stocate in vectorul trackerFileInfo, iar swarmInfoList este actualizat pentru a include peer-urile
care detin fisierele.

Dupa acest lucru tracker ul trimite informatii fiecarui client pentru a sti despre fisierele disponibile in reatea,
astfel tracker ul este ca un intermediar intre clienti. Important este sa nu retinem informatiile despre segmente
pentru fiecare client. acest lucru nu ar respecta conditiile protocolului.
	2.	Intermediarul adica tracker ul va intra intr o bucla pentru a astepta cereri de la clienti
si va termina in cazul in care tot clientii au terminat descarcarea fisierelor(simularea).
Daca un peer a terminat descarcarea tuturor fisierelor dorite, acesta trimite un mesaj “FINISH”.
Daca exitCount ajunge la 0 (toti peers au terminat descarcarile):
Tracker-ul trimite mesajul “END” catre fiecare peer pentru a opri thread-urile de upload.
Bucla principala este incheiata.

Per general in tracker salvam o lista actualizata a informatiilor despre fisiere si segmente(hash) din retea
si o lista a swarm urilor(explicand mai sus)
Astfel swarm ul permite fiecarui peer sa obtina segmentele necesare de la alte peers uri.
	3.	Functia de download este responsabila pentru gestionarea cererilor de descarcare trimise  de la un peer
catre alti clienti care detin segmentele fisierului dorit. Un client trimite altui client mesj cu hash ul dorit si
asteapta un raspuns de la acesta.
Simularea descarcarii am facut o mai eficienta folosit algoritmul Round Robin(am vazut pe forum ca e ok:),
Astfel la inceput cererile se duceau doar catre un peer ce ducea la supraincarcarea lui.
Am rezolvat prin a imparti cererile de decarcare catre mai multi clienti
Exemplu: Daca segmentul 1 este trimis catre Peer 1, segmentul 2 este trimis catre Peer 2.....
La primirea unui segment, segmentul este salvat local, face o verificare intre hash ul primit si cel dorit.
Dupa ce s au descarcat toate segmentele si fisierul este intreg il salvam. 
Dupa ce s au descarcat toate fisierele trimtiem "FINISH"
   4.    Upload asteapta mesaje de la tracker sau de la alti clienti si in cazul clientilor verifica 
existenta hash ului primit in propriile fisiere detinute si trimite un mesaj corespunzator.

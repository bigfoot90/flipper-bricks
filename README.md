# 🧱 Bricks per Flipper Zero

> *Ricordi i pomeriggi passati a incastrare blocchi che cadevano, con una musichetta russa che non ti usciva più dalla testa?*

Il gioco del genere è sempre lo stesso, e funziona da quarant'anni. Dall'alto scendono pezzi formati da quattro quadratini. Tu li sposti, li ruoti e li incastri. Quando una riga è piena sparisce, e il resto della pila scende. Se la pila arriva in cima, la partita è finita.

**Bricks** porta questo tipo di puzzle sul **Flipper Zero**. Si gioca tenendo il Flipper **in verticale**, come una console tascabile. In sottofondo suona **Korobeiniki**, la vecchia canzone popolare russa.

---

## 🎮 Come si gioca

Tieni il Flipper in verticale, con i tasti in basso.

| Tasto | Cosa fa |
|---|---|
| ⬅️ ➡️ | spostano il pezzo (tenendo premuto si ripete) |
| ⬇️ | discesa veloce finché lo tieni premuto |
| ⬆️ / OK | ruotano il pezzo |
| Back | pausa (e salvataggio) |
| Back tenuto premuto | esce dal gioco, salvando la partita |

Nel **menu**, con ⬆️ ⬇️ scegli la riga e con ⬅️ ➡️ o OK cambi l'opzione.

## 🖥️ Lo schermo

- In alto a sinistra ci sono il **punteggio**, il **livello** (`Lv`) e le **righe** completate (`R`).
- In alto a destra c'è il **pezzo successivo**.
- Il **campo** è di 10 × 18 caselle e occupa quasi tutto lo schermo.
- Un contorno vuoto, il **pezzo fantasma**, mostra dove atterrerà il pezzo che sta cadendo.

## 🏆 Punteggio e livelli

| Righe completate insieme | Punti |
|---|---|
| 1 | 100 × livello |
| 2 | 300 × livello |
| 3 | 500 × livello |
| 4 | 800 × livello |

Ogni riga fatta scendere con ⬇️ vale **1 punto** in più. Ogni **10 righe** si sale di livello: i pezzi cadono più veloci e la musica accelera un po'.

## ✨ Caratteristiche

- **Schermata di apertura**: il logo è un pezzo del gioco fatto di mattoncini grandi, e le lettere di "BRICKS", anche loro fatte di mattoncini, cadono una alla volta. Si salta con un tasto qualsiasi.
- **Righe che lampeggiano**: quando completi una riga, questa lampeggia per un attimo prima di sparire.
- **Pezzi più equi**: ogni gruppo di 7 pezzi li contiene tutti e sette, in ordine casuale. Non capitano lunghe attese senza il pezzo dritto da quattro.
- **Rotazione vicino ai muri**: se il pezzo ruotato urterebbe un bordo, il gioco prova a spostarlo di lato di una o due caselle.
- **Musica Korobeiniki**: suona durante la partita e si ferma in pausa. Se sei in riunione, puoi spegnerla dal menu con **Suoni: No**; la scelta resta memorizzata.
- **Record salvato** sulla scheda SD e mostrato nel menu.
- **Pausa = partita salvata**: ogni volta che metti in pausa o esci, la partita viene salvata. Puoi chiudere il gioco o spegnere il Flipper e riprenderla più tardi dal menu con **Continua**. Riprende in pausa, esattamente com'era, musica compresa.
- **Schermo sempre acceso** mentre giochi.

I dati stanno sulla SD in `apps_data/bricks/`:
- `bricks.save` contiene il record e la scelta dei suoni;
- `bricks.game` contiene la partita in corso, e viene cancellato quando la partita finisce.

## 📲 Installazione

Copia il file `.fap` sulla scheda SD del Flipper in `apps/Games/`, con qFlipper o con [lab.flipper.net](https://lab.flipper.net). Poi apri **Apps → Games → Bricks**.

Se hai installato [ufbt](https://github.com/flipperdevices/flipperzero-ufbt) e il Flipper è collegato via USB, dalla cartella del gioco basta un comando per compilarlo, installarlo e avviarlo:

```sh
ufbt launch
```

### ⚠️ Se compare "App Too Old"

Il gioco non è rotto. Ogni app per Flipper Zero viene compilata per una specifica versione del firmware, e **non è possibile creare un unico file compatibile con tutte**. Se il tuo Flipper ha un firmware più recente o più vecchio di quello usato per compilare il gioco, all'avvio vedrai `App Too Old` oppure `App Too New`.

La soluzione è ricompilarlo per il tuo firmware. Apri il terminale nella cartella del gioco e lancia:

```sh
ufbt update   # scarica gli strumenti per l'ultimo firmware ufficiale
ufbt          # compila il gioco
```

Poi copia sul Flipper il nuovo file `.fap` che trovi nella cartella `dist/`.

## 🔧 Per chi vuole modificarlo

Tutto il gioco sta in `bricks.c`. Alcune impostazioni utili si cambiano in cima al file:

| Costante | Cosa regola |
|---|---|
| `VOLUME` | volume della musica, da `0.0` a `1.0` |
| `FAST_DROP_MS` | velocità della discesa con ⬇️ (ms per casella) |
| `CLEAR_MS` / `FLASH_MS` | durata e ritmo del lampeggio delle righe |
| `BH` / `CELL` | numero di righe e dimensione delle caselle |

---

*Bricks è un progetto amatoriale, fatto per divertimento. È un gioco originale ispirato al genere dei puzzle a blocchi che cadono, e non è affiliato né approvato da alcun produttore di videogiochi. Korobeiniki è una canzone popolare russa dell'Ottocento, di pubblico dominio.*

Buona partita, e occhio alla pila! 🧱

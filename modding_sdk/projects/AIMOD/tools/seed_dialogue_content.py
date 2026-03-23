from __future__ import annotations

import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DB_PATH = ROOT / "data" / "aimod_catalog.db"


KEYWORD_RULES = [
    ("hola", "greet", 3), ("buenas", "greet", 3), ("hey", "greet", 2), ("ey", "greet", 2),
    ("saludos", "greet", 3), ("que tal", "greet", 2), ("amigo", "greet", 1), ("bro", "greet", 1),
    ("donde", "ask", 3), ("como", "ask", 2), ("cuando", "ask", 2), ("por que", "ask", 3),
    ("porque", "ask", 3), ("quien", "ask", 2), ("que sabes", "ask", 3), ("ayuda", "ask", 3),
    ("necesito", "ask", 2), ("puedes", "ask", 2), ("quiero saber", "ask", 3), ("informacion", "ask", 2),
    ("idiota", "insult", 4), ("imbecil", "insult", 4), ("estupido", "insult", 4), ("mierda", "insult", 3),
    ("pendejo", "insult", 5), ("cabrón", "insult", 4), ("cabron", "insult", 4), ("perro", "insult", 3),
    ("payaso", "insult", 3), ("basura", "insult", 3), ("maldito", "insult", 4), ("loco de mierda", "insult", 5),
    ("te mato", "threaten", 6), ("disparo", "threaten", 5), ("te rompo", "threaten", 5), ("te va mal", "threaten", 4),
    ("cuidado", "threaten", 2), ("quieto", "threaten", 2), ("manos arriba", "threaten", 4), ("te bajo", "threaten", 5),
    ("calma", "calm", 4), ("tranquilo", "calm", 4), ("tranqui", "calm", 4), ("perdon", "calm", 4),
    ("disculpa", "calm", 4), ("paz", "calm", 3), ("no busco problemas", "calm", 5), ("sin bronca", "calm", 4),
    ("sigueme", "recruit", 5), ("ven conmigo", "recruit", 5), ("ven", "recruit", 2), ("ayudame", "recruit", 4),
    ("cubreme", "recruit", 5), ("vamos", "recruit", 2), ("acompáñame", "recruit", 5), ("acompañame", "recruit", 5),
    ("vete", "dismiss", 4), ("largate", "dismiss", 5), ("fuera", "dismiss", 3), ("adios", "dismiss", 2),
    ("chao", "dismiss", 2), ("desaparece", "dismiss", 4), ("pierdete", "dismiss", 4),
]


REPLY_DATA = {
    ("default", "greet", "neutral"): [
        "Si, te escucho.",
        "Aja, que quieres ahora?",
        "Habla rapido.",
        "Todo bien, dime.",
        "Bueno, suelta lo que vas a decir.",
    ],
    ("default", "greet", "friendly"): [
        "Que onda, todo tranquilo?",
        "Buenas, que necesitas?",
        "Dale, te escucho.",
        "Todo bien por aqui.",
    ],
    ("default", "ask", "neutral"): [
        "Depende de lo que preguntes.",
        "Si lo se, te digo.",
        "Habla claro.",
        "No me hagas perder tiempo.",
        "Ve al grano.",
    ],
    ("default", "ask", "friendly"): [
        "Si puedo ayudar, te ayudo.",
        "A ver, dime bien.",
        "Preguntame nomas.",
    ],
    ("default", "insult", "dismiss"): [
        "Bajale dos rayitas.",
        "No me hables asi.",
        "Lavate la boca antes de hablarme.",
        "No ando para tus tonteras.",
    ],
    ("default", "insult", "attack"): [
        "Quieres problema? lo encontraste.",
        "Sigue hablando y veras.",
        "Ya me cansaste.",
    ],
    ("default", "threaten", "warn"): [
        "Mide tus palabras.",
        "No me impresiona tu show.",
        "Prueba y vemos que pasa.",
    ],
    ("default", "threaten", "flee"): [
        "Hey, tranquilo, ya me voy.",
        "No busco bronca, calmate.",
        "Vale, vale, me largo.",
    ],
    ("default", "threaten", "attack"): [
        "A mi no me amenazas.",
        "Pues hazlo, a ver si puedes.",
        "Te crees muy valiente?",
    ],
    ("default", "calm", "neutral"): [
        "Bueno, bajemosle.",
        "Dale, sin drama.",
        "Listo, tranqui.",
    ],
    ("default", "calm", "friendly"): [
        "Asi mejor, compa.",
        "Perfecto, hablemos bien.",
        "Eso ya suena mejor.",
    ],
    ("default", "recruit", "follow"): [
        "Va, te sigo.",
        "Bueno, voy contigo.",
        "Dale, te acompaño.",
    ],
    ("default", "recruit", "refuse"): [
        "No, tengo mis asuntos.",
        "Paso, no me conviene.",
        "Hoy no, compa.",
    ],
    ("default", "dismiss", "dismiss"): [
        "Bueno, ya me voy.",
        "Listo, desaparezco.",
        "Como quieras.",
    ],

    ("ambient", "greet", "neutral"): [
        "Que tal, vecino?",
        "Si, dime rapido.",
        "Todo bien, que pasa?",
        "Aja, te escucho.",
    ],
    ("ambient", "ask", "friendly"): [
        "Si se algo, te aviso.",
        "Capaz puedo ayudarte.",
        "Mmm, a ver si recuerdo.",
    ],
    ("ambient", "recruit", "refuse"): [
        "No me metas en tus lios.",
        "Paso, hermano.",
        "Ni loco voy contigo.",
    ],
    ("ambient", "calm", "friendly"): [
        "Asi me gusta, sin escandalo.",
        "Bueno, paz entonces.",
        "Ya, todo tranquilo.",
    ],
    ("ambient", "insult", "dismiss"): [
        "Que te pasa, enfermo?",
        "Uy, que agresivo.",
        "Anda a molestar a otro.",
    ],

    ("gang", "greet", "dismiss"): [
        "Habla rapido o largate.",
        "No me hagas perder el tiempo.",
        "Te estoy mirando, loco.",
    ],
    ("gang", "ask", "neutral"): [
        "Depende si me conviene decirte.",
        "Tal vez sepa algo, tal vez no.",
        "No todo se pregunta asi nomas.",
    ],
    ("gang", "threaten", "attack"): [
        "Vas a tener que respaldar eso.",
        "A ver si tan bravo eres.",
        "Conmigo no te hagas el loco.",
    ],
    ("gang", "recruit", "follow"): [
        "Va, pero no me falles.",
        "Te sigo, jefe.",
        "Bueno, hagamos esto.",
    ],
    ("gang", "recruit", "refuse"): [
        "No trabajo gratis.",
        "Consigue a otro.",
        "Hoy no me sumo.",
    ],

    ("ballas", "greet", "dismiss"): [
        "Que miras, loco?",
        "Habla y vete.",
        "Si no traes negocio, corre.",
        "No me gusta tu cara, pero te escucho.",
    ],
    ("ballas", "ask", "dismiss"): [
        "Preguntas demasiado.",
        "No sabes con quien hablas.",
        "Eso no te importa, perro.",
        "Mete tu nariz en otro lado.",
    ],
    ("ballas", "ask", "neutral"): [
        "Depende del trato.",
        "Tal vez te diga algo, tal vez no.",
        "Si me conviene, te respondo.",
    ],
    ("ballas", "insult", "warn"): [
        "Repite eso si te atreves.",
        "Ya te me estas pasando.",
        "Te falta barrio pa hablar asi.",
    ],
    ("ballas", "insult", "attack"): [
        "Ahora si te gano, perro.",
        "Te voy a bajar los humos.",
        "Ya firmaste tu problema.",
        "Eso fue lo ultimo que dijiste tranquilo.",
    ],
    ("ballas", "threaten", "attack"): [
        "Ven y pruebalo.",
        "A mi no me tiemblan las manos.",
        "Te voy a dejar tirado.",
        "No sabes con quien te metiste.",
    ],
    ("ballas", "calm", "neutral"): [
        "Mas te vale seguir tranquilo.",
        "Bueno, baja la voz entonces.",
        "No me hagas cambiar de idea.",
    ],
    ("ballas", "recruit", "follow"): [
        "Va, te cubro un rato.",
        "Te sigo, pero sin fallar.",
        "Bueno, mueve las patas.",
    ],
    ("ballas", "recruit", "refuse"): [
        "Yo no sigo a cualquiera.",
        "Buscate otro soldado.",
        "No eres mi jefe.",
    ],
    ("ballas", "dismiss", "dismiss"): [
        "Ya, me voy... por ahora.",
        "Sigue caminando, entonces.",
        "Nos vemos, si sobrevives.",
    ],

    ("police", "greet", "warn"): [
        "Mantenga distancia y diga lo que necesita.",
        "Sea breve, ciudadano.",
        "No haga movimientos raros.",
    ],
    ("police", "ask", "warn"): [
        "Hable claro y sin rodeos.",
        "Si es una denuncia, hagala ahora.",
        "Explique la situacion.",
        "Lo estoy escuchando, pero rapido.",
    ],
    ("police", "insult", "warn"): [
        "Baje el tono inmediatamente.",
        "Ultima advertencia.",
        "Modere su lenguaje.",
    ],
    ("police", "insult", "attack"): [
        "Queda detenido.",
        "Eso es resistencia, al suelo.",
        "Unidad, sujeto hostil.",
    ],
    ("police", "threaten", "attack"): [
        "Arma verbal o no, queda reducido.",
        "Eso fue una amenaza directa.",
        "Manos donde pueda verlas, ya.",
        "No me obligue a usar la fuerza.",
    ],
    ("police", "calm", "neutral"): [
        "Bien, mantenga la calma.",
        "Asi esta mejor.",
        "Perfecto, coopere y no habra problema.",
    ],
    ("police", "recruit", "refuse"): [
        "No trabajo para civiles.",
        "Negativo, siga su camino.",
        "No es asi como funciona esto.",
    ],
    ("police", "dismiss", "warn"): [
        "Circule, pero mantengase visible.",
        "Retirese sin causar problemas.",
        "Muévase y no vuelva a acercarse asi.",
    ],

    ("special", "greet", "neutral"): [
        "Hmm... te escucho.",
        "No todos se acercan asi.",
        "Curioso... habla.",
    ],
    ("special", "ask", "neutral"): [
        "Tal vez tenga una respuesta para ti.",
        "Esa pregunta tiene varias capas.",
        "No esperaba eso, pero adelante.",
    ],
    ("special", "calm", "friendly"): [
        "Bien, prefiero la paz.",
        "Excelente, mantengamos el equilibrio.",
        "Mucho mejor asi.",
    ],
}


def ensure_schema(cur: sqlite3.Cursor) -> None:
    cur.execute(
        """
        CREATE TABLE IF NOT EXISTS interaction_keyword_rules (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            keyword TEXT NOT NULL,
            action_key TEXT NOT NULL,
            weight INTEGER NOT NULL DEFAULT 1
        )
        """
    )
    cur.execute(
        """
        CREATE UNIQUE INDEX IF NOT EXISTS idx_interaction_keyword_rules_unique
        ON interaction_keyword_rules(keyword, action_key)
        """
    )
    cur.execute(
        """
        CREATE UNIQUE INDEX IF NOT EXISTS idx_interaction_replies_unique
        ON interaction_replies(group_name, action_key, reaction_key, reply_text_es)
        """
    )


def seed_keywords(cur: sqlite3.Cursor) -> None:
    cur.executemany(
        """
        INSERT OR IGNORE INTO interaction_keyword_rules(keyword, action_key, weight)
        VALUES (?, ?, ?)
        """,
        KEYWORD_RULES,
    )


def seed_replies(cur: sqlite3.Cursor) -> None:
    rows = []
    for (group_name, action_key, reaction_key), replies in REPLY_DATA.items():
        for reply in replies:
            rows.append((group_name, action_key, reaction_key, reply))

    cur.executemany(
        """
        INSERT OR IGNORE INTO interaction_replies(group_name, action_key, reaction_key, reply_text_es)
        VALUES (?, ?, ?, ?)
        """,
        rows,
    )


def main() -> None:
    conn = sqlite3.connect(DB_PATH)
    cur = conn.cursor()
    ensure_schema(cur)
    seed_keywords(cur)
    seed_replies(cur)
    conn.commit()

    keyword_count = cur.execute("SELECT COUNT(*) FROM interaction_keyword_rules").fetchone()[0]
    reply_count = cur.execute("SELECT COUNT(*) FROM interaction_replies").fetchone()[0]
    conn.close()

    print(f"keyword_rules={keyword_count}")
    print(f"interaction_replies={reply_count}")


if __name__ == "__main__":
    main()

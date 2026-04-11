from flask import Flask, render_template, request, jsonify
from flask_sqlalchemy import SQLAlchemy
from config import Config
from models import db, Kunde, Auftrag, Angebot, Rechnung, CRM
from sqlalchemy import or_, func
from datetime import datetime
import os

app = Flask(__name__)
app.config.from_object(Config)
db.init_app(app)

@app.route('/')
def index():
    return render_template('index.html', firma="A. Hasshold GmbH", slogan="Baumpflege & Gartenpflege")

@app.route('/kunden')
def kunden():
    suchbegriff = request.args.get('q', '').strip()
    sort_by = request.args.get('sort', 'Name')
    sort_order = request.args.get('order', 'asc')
    
    query = Kunde.query.filter(Kunde.GebannterKunde == None)
    
    if suchbegriff:
        suchbegriff_like = f"{suchbegriff}%"
        query = query.filter(
            or_(
                Kunde.Name.ilike(suchbegriff_like),
                Kunde.Strasse.ilike(suchbegriff_like),
                Kunde.Ort.ilike(suchbegriff_like),
                Kunde.PLZ.ilike(suchbegriff_like),
                Kunde.Telefon.ilike(suchbegriff_like)
            )
        )
    
    if hasattr(Kunde, sort_by):
        sort_column = getattr(Kunde, sort_by)
        if sort_order == 'desc':
            query = query.order_by(sort_column.desc())
        else:
            query = query.order_by(sort_column.asc())
    
    kunden = query.limit(100).all()
    anzahl = query.count()
    
    return render_template('kunden.html', 
                         kunden=kunden, 
                         suchbegriff=suchbegriff,
                         anzahl=anzahl,
                         sort_by=sort_by,
                         sort_order=sort_order)

@app.route('/api/kunden')
def api_kunden():
    suchbegriff = request.args.get('q', '').strip()
    
    query = Kunde.query.filter(Kunde.GebannterKunde == None)
    
    if suchbegriff:
        suchbegriff_like = f"{suchbegriff}%"
        query = query.filter(
            or_(
                Kunde.Name.ilike(suchbegriff_like),
                Kunde.Strasse.ilike(suchbegriff_like),
                Kunde.Ort.ilike(suchbegriff_like)
            )
        )
    
    kunden = query.limit(50).all()
    return jsonify([k.to_dict() for k in kunden])

@app.route('/kunde/<int:kunde_id>')
def kunde_detail(kunde_id):
    kunde = Kunde.query.get_or_404(kunde_id)
    
    auftraege = Auftrag.query.filter_by(Kunde_idKunde=kunde_id).order_by(Auftrag.Auftragsdatum.desc()).limit(20).all()
    angebote = Angebot.query.filter_by(Kunde_idKunde=kunde_id).order_by(Angebot.Angebotsdatum.desc()).limit(10).all()
    
    auftrag_ids = [a.idAuftrag for a in auftraege]
    rechnungen = Rechnung.query.filter(Rechnung.Auftrag_idAuftrag.in_(auftrag_ids)).all() if auftrag_ids else []
    
    crm_eintraege = CRM.query.filter_by(Kunde_idKunde=kunde_id).order_by(CRM.Datum.desc()).limit(10).all()
    
    gesamtwert = db.session.query(func.sum(Auftrag.Auftragswert)).filter(Auftrag.Kunde_idKunde == kunde_id).scalar() or 0
    
    return render_template('kunde_detail.html', 
                         kunde=kunde,
                         auftraege=auftraege,
                         angebote=angebote,
                         rechnungen=rechnungen,
                         crm_eintraege=crm_eintraege,
                         gesamtwert=gesamtwert)

@app.route('/api/kunde/<int:kunde_id>')
def api_kunde_detail(kunde_id):
    kunde = Kunde.query.get_or_404(kunde_id)
    
    auftraege = Auftrag.query.filter_by(Kunde_idKunde=kunde_id).order_by(Auftrag.Auftragsdatum.desc()).all()
    angebote = Angebot.query.filter_by(Kunde_idKunde=kunde_id).order_by(Angebot.Angebotsdatum.desc()).all()
    
    return jsonify({
        'kunde': kunde.to_dict(),
        'auftraege': [a.to_dict() for a in auftraege],
        'angebote': [a.to_dict() for a in angebote]
    })

@app.route('/auftraege')
def auftraege():
    suchbegriff = request.args.get('q', '').strip()
    status = request.args.get('status', '')
    sort_by = request.args.get('sort', 'Auftragsdatum')
    sort_order = request.args.get('order', 'desc')
    
    query = db.session.query(Auftrag, Kunde).join(Kunde, Auftrag.Kunde_idKunde == Kunde.idKunde).filter(Kunde.GebannterKunde == None)
    
    if suchbegriff:
        suchbegriff_like = f"{suchbegriff}%"
        query = query.filter(
            or_(
                Kunde.Name.ilike(suchbegriff_like),
                Auftrag.Bauvorhaben.ilike(suchbegriff_like),
                Auftrag.Ort.ilike(suchbegriff_like)
            )
        )
    
    if status == 'erledigt':
        query = query.filter(or_(
            Auftrag.Erledigt == 'Ausgeführt',
            Auftrag.Erledigt == 'Ausgeführt ',
            Auftrag.Erledigt == 'Storno'
        ))
    elif status == 'offen':
        query = query.filter(or_(
            Auftrag.Erledigt == 'offen',
            Auftrag.Erledigt == 'in Arbeit/z.T. offen'
        ))
    
    if hasattr(Auftrag, sort_by):
        sort_column = getattr(Auftrag, sort_by)
        if sort_order == 'desc':
            query = query.order_by(sort_column.desc())
        else:
            query = query.order_by(sort_column.asc())
    else:
        query = query.order_by(Auftrag.Auftragsdatum.desc())
    
    ergebnisse = query.limit(200).all()
    anzahl = len(ergebnisse)
    
    gesamtwert = db.session.query(func.sum(Auftrag.Auftragswert)).filter(
        or_(
            Auftrag.Erledigt == 'offen',
            Auftrag.Erledigt == 'in Arbeit/z.T. offen'
        )
    ).scalar() or 0
    
    return render_template('auftraege.html',
                         auftraege=ergebnisse,
                         suchbegriff=suchbegriff,
                         status=status,
                         anzahl=anzahl,
                         gesamtwert=gesamtwert,
                         sort_by=sort_by,
                         sort_order=sort_order)

@app.route('/rechnungen')
def rechnungen():
    suchbegriff = request.args.get('q', '').strip()
    status = request.args.get('status', '')
    sort_by = request.args.get('sort', 'Rechnungsdatum')
    sort_order = request.args.get('order', 'desc')
    
    query = db.session.query(Rechnung, Auftrag, Kunde).join(Auftrag, Rechnung.Auftrag_idAuftrag == Auftrag.idAuftrag).join(Kunde, Auftrag.Kunde_idKunde == Kunde.idKunde).filter(Kunde.GebannterKunde == None)
    
    if suchbegriff:
        suchbegriff_like = f"{suchbegriff}%"
        query = query.filter(
            or_(
                Kunde.Name.ilike(suchbegriff_like),
                Rechnung.Rechnungsnummer == suchbegriff if suchbegriff.isdigit() else False
            )
        )
    
    if status == 'bezahlt':
        query = query.filter(Rechnung.Bezahldatum != None)
    elif status == 'ausstehend':
        query = query.filter(Rechnung.Bezahldatum == None)
    
    if hasattr(Rechnung, sort_by):
        sort_column = getattr(Rechnung, sort_by)
        if sort_order == 'desc':
            query = query.order_by(sort_column.desc())
        else:
            query = query.order_by(sort_column.asc())
    else:
        query = query.order_by(Rechnung.Rechnungsdatum.desc())
    
    ergebnisse = query.limit(200).all()
    anzahl = len(ergebnisse)
    
    ausstehend = db.session.query(func.sum(Rechnung.Rechnungswert)).filter(Rechnung.Bezahldatum == None).scalar() or 0
    bezahlt = db.session.query(func.sum(Rechnung.Rechnungswert)).filter(Rechnung.Bezahldatum != None).scalar() or 0
    
    return render_template('rechnungen.html',
                         rechnungen=ergebnisse,
                         suchbegriff=suchbegriff,
                         status=status,
                         anzahl=anzahl,
                         ausstehend=ausstehend,
                         bezahlt=bezahlt,
                         sort_by=sort_by,
                         sort_order=sort_order)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8000, debug=True)

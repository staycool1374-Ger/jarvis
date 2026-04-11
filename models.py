from flask_sqlalchemy import SQLAlchemy
from datetime import datetime

db = SQLAlchemy()

class Kunde(db.Model):
    __tablename__ = 'Kunde'
    
    idKunde = db.Column(db.Integer, primary_key=True)
    Name = db.Column(db.String(45))
    Strasse = db.Column(db.String(45))
    Ort = db.Column(db.String(45))
    PLZ = db.Column(db.String(5))
    Stammkunde = db.Column(db.String(45))
    Telefon = db.Column(db.String(45))
    GebannterKunde = db.Column(db.Text)
    Anrede = db.Column(db.String(45))
    Titel = db.Column(db.String(45))
    erzeugt_am = db.Column(db.DateTime, default=datetime.utcnow)
    update_am = db.Column(db.DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)
    letzter_benutzer = db.Column(db.String(45))
    
    auftraege = db.relationship('Auftrag', backref='kunde', lazy=True)
    angebote = db.relationship('Angebot', backref='kunde', lazy=True)
    crm_eintraege = db.relationship('CRM', backref='kunde', lazy=True)
    
    def to_dict(self):
        return {
            'idKunde': self.idKunde,
            'Name': self.Name,
            'Strasse': self.Strasse,
            'Ort': self.Ort,
            'PLZ': self.PLZ,
            'Stammkunde': self.Stammkunde,
            'Telefon': self.Telefon,
            'GebannterKunde': self.GebannterKunde,
            'Anrede': self.Anrede,
            'Titel': self.Titel
        }

class Auftrag(db.Model):
    __tablename__ = 'Auftrag'
    
    idAuftrag = db.Column(db.Integer, primary_key=True)
    Bauvorhaben = db.Column(db.String(45))
    Ort = db.Column(db.String(45))
    SKT = db.Column(db.Boolean, default=False)
    HB = db.Column(db.Boolean, default=False)
    Auftragsdatum = db.Column(db.Date)
    Auftragsart = db.Column(db.String(45))
    Kunde_idKunde = db.Column(db.Integer, db.ForeignKey('Kunde.idKunde'))
    Erledigungsdatum = db.Column(db.Date)
    Halteverbote = db.Column(db.Boolean, default=False)
    KleineHB = db.Column(db.Boolean, default=False)
    Genehmigung = db.Column(db.String(45))
    Priorität = db.Column(db.String(45), default='Normal')
    Bemerkung = db.Column(db.Text)
    Auftragswert = db.Column(db.Numeric(19, 2))
    Erledigt = db.Column(db.String(45))
    erzeugt_am = db.Column(db.DateTime, default=datetime.utcnow)
    update_am = db.Column(db.DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)
    letzter_benutzer = db.Column(db.String(45))
    Angebotsnr = db.Column(db.String(10))
    Pressemittelung = db.Column(db.Boolean)
    
    def to_dict(self):
        return {
            'idAuftrag': self.idAuftrag,
            'Bauvorhaben': self.Bauvorhaben,
            'Ort': self.Ort,
            'Auftragsdatum': self.Auftragsdatum.isoformat() if self.Auftragsdatum else None,
            'Auftragsart': self.Auftragsart,
            'Erledigungsdatum': self.Erledigungsdatum.isoformat() if self.Erledigungsdatum else None,
            'Erledigt': self.Erledigt,
            'Auftragswert': float(self.Auftragswert) if self.Auftragswert else 0,
            'Priorität': self.Priorität
        }

class Angebot(db.Model):
    __tablename__ = 'Angebot'
    
    id = db.Column(db.Integer, primary_key=True)
    Angebotsdatum = db.Column(db.Date)
    Angebotswert = db.Column(db.Numeric(19, 2))
    Angebotjahr = db.Column(db.SmallInteger)
    LfdNr = db.Column(db.String(10))
    Pfad = db.Column(db.String(255))
    Kunde_idKunde = db.Column(db.Integer, db.ForeignKey('Kunde.idKunde'))
    
    def to_dict(self):
        return {
            'id': self.id,
            'Angebotsdatum': self.Angebotsdatum.isoformat() if self.Angebotsdatum else None,
            'Angebotswert': float(self.Angebotswert) if self.Angebotswert else 0,
            'Angebotjahr': self.Angebotjahr,
            'LfdNr': self.LfdNr
        }

class Rechnung(db.Model):
    __tablename__ = 'Rechnung'
    
    idRechnung = db.Column(db.Integer, primary_key=True)
    Bezahldatum = db.Column(db.Date)
    Rechnungsdatum = db.Column(db.Date)
    Rechnungsnummer = db.Column(db.Integer)
    Bemerkung = db.Column(db.String(70))
    Rechnungswert = db.Column(db.Numeric(19, 2))
    Pfad = db.Column(db.String(255))
    Auftrag_idAuftrag = db.Column(db.Integer, db.ForeignKey('Auftrag.idAuftrag'))
    Mahnung = db.Column(db.String(45))
    
    def to_dict(self):
        return {
            'idRechnung': self.idRechnung,
            'Bezahldatum': self.Bezahldatum.isoformat() if self.Bezahldatum else None,
            'Rechnungsdatum': self.Rechnungsdatum.isoformat() if self.Rechnungsdatum else None,
            'Rechnungsnummer': self.Rechnungsnummer,
            'Rechnungswert': float(self.Rechnungswert) if self.Rechnungswert else 0,
            'Bezahldatum': self.Bezahldatum.isoformat() if self.Bezahldatum else None
        }

class CRM(db.Model):
    __tablename__ = 'CRM'
    
    idCRM = db.Column(db.Integer, primary_key=True)
    Kunde_idKunde = db.Column(db.Integer, db.ForeignKey('Kunde.idKunde'))
    Notiz = db.Column(db.Text)
    Datum = db.Column(db.Date)
    Mitarbeiter = db.Column(db.String(45))
    Zu_Besuchen = db.Column(db.Boolean)
    
    def to_dict(self):
        return {
            'idCRM': self.idCRM,
            'Notiz': self.Notiz,
            'Datum': self.Datum.isoformat() if self.Datum else None,
            'Mitarbeiter': self.Mitarbeiter
        }

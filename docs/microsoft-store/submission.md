# Submitting Dropocalypse to the Microsoft Store

The name is already reserved.

- Store ID: `9N1TJ9D5R5C8`
- Package identity: `AllenRobberson.Dropocalypse`
- Publisher: `CN=E2C97CD9-87D6-4B67-95B2-E279CF095C7A`
- Publisher display name: Allen Robberson

Every Windows build produces `Dropocalypse-<version>-Store.msix` (artifact **Dropocalypse-Microsoft-Store**). The Store edition has no built-in updater. The Store signs the package; the file we upload is unsigned on purpose.

Microsoft does not let a script create the app or answer the age-ratings questionnaire. A person does that once in Partner Center. After that, the **Microsoft Store** workflow can upload new packages.

Listing text, search terms and the screenshot note are in [listing.md](listing.md). Screenshots are the 2560×1600 images in `docs/app-store/screenshots/`.

## 1. First submission, in the browser

Open [Partner Center](https://partner.microsoft.com/dashboard/apps-and-games/overview) → **Dropocalypse** → **Start submission**.

1. **Pricing and availability.** Free. No in-app purchases. Available in the markets you want.
2. **Properties.** Category **Music**. Privacy policy URL: `https://github.com/allenrobberson/dropocalypse/blob/main/PRIVACY.md`. Website and support contact are the GitHub repository and its Discussions page.
3. **Age ratings.** App type **All other app types** (it is not a game and not a social app). Answer no to violence, sexual content, language, controlled substances, user-generated content, sharing personal information, and purchases. If a question asks about flashing lights, answer yes: the visuals can flash. That note is already in the description.
4. **Store listings**, English. Paste the short description and description from [listing.md](listing.md). Upload at least one screenshot from `docs/app-store/screenshots/` (the Store wants 1366×768 or larger; these are 2560×1600). Show the visualizer in use, not a blank window.
5. **Packages.** Download `Dropocalypse-<version>-Store.msix` from the latest successful **Build** run (artifact **Dropocalypse-Microsoft-Store**) and upload it. The version inside the package is `major.minor.build.0` from `docs/guide-version.txt` (currently 1.2.11, so `1.2.11.0`).
6. **Submit for certification.** Certification takes a few hours, sometimes a few days. Wait until the app is published before using the workflow for updates.

## 2. Let GitHub upload later versions

Do this once. Put the secret only in GitHub. Don't paste the client secret into chat or commit it.

1. Sign in to [Microsoft Entra admin center](https://entra.microsoft.com/) with the tenant that Partner Center uses (Account settings → Tenants, or associate one there).
2. Copy the **Tenant ID** from Identity → Overview.
3. Identity → Applications → App registrations → **New registration**. Name it `Dropocalypse Store CI`. Accounts in this organizational directory only. No redirect URI. Register, then copy the **Application (client) ID**.
4. On that app, Certificates & secrets → **New client secret**. Copy the **Value** immediately. A 24-month expiry is fine.
5. In Partner Center, Account settings → User management → **Microsoft Entra applications**. Add that application and give it the **Manager** role.
6. In Partner Center, Account settings → Identifiers (sometimes called Organization profile or Developer settings). Copy the **Seller ID** (also called Publisher ID). It is not the Store ID.
7. On GitHub, open this repository → Settings → Secrets and variables → Actions → New repository secret. Add exactly these four:

| Secret name | Value |
|---|---|
| `AZURE_AD_TENANT_ID` | Tenant ID |
| `AZURE_AD_APPLICATION_CLIENT_ID` | Application (client) ID |
| `AZURE_AD_APPLICATION_SECRET` | Client secret value |
| `SELLER_ID` | Seller / Publisher ID |

## 3. Run the workflow

Every release now does this by itself: when the Build workflow publishes a release, its last job starts the **Microsoft Store** workflow for the same version. Run it by hand only to resend a version.

Actions → **Microsoft Store** → **Run workflow**. Leave the version empty to use `docs/guide-version.txt`, or type a higher `1.2.7` when that version is ready. **Upload the package to Partner Center** stays on.

The workflow builds the MSIX, then runs `msstore publish` for Store ID `9N1TJ9D5R5C8`. Each upload must use a higher version than the one already in Partner Center.

If Partner Center still has a draft submission that was created in the browser, the CLI cannot upload into it. Delete that draft and run the workflow again. Don't do that to a submission that is already in certification.

After a successful run, certification starts on its own. The listing, age ratings and price from the previous submission are kept.
